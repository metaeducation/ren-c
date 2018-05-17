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
    REBARR *array = Make_Array(VAL_ACT_NUM_PARAMS(func));
    REBVAL *param = VAL_ACT_PARAMS_HEAD(func);

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
    REBARR *array = Make_Array(VAL_ACT_NUM_PARAMS(func));
    REBVAL *typeset = VAL_ACT_PARAMS_HEAD(func);

    for (; NOT_END(typeset); typeset++) {
        assert(IS_TYPESET(typeset));

        REBVAL *value = Alloc_Tail_Array(array);
        Move_Value(value, typeset);

        // !!! It's already a typeset, but this will clear out the header
        // bits.  This may not be desirable over the long run (what if
        // a typeset wishes to encode hiddenness, protectedness, etc?)
        //
        RESET_VAL_HEADER(value, REB_TYPESET);
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
    assert(ANY_ARRAY(spec));

    uintptr_t header_bits = 0;

#if !defined(NDEBUG)
    //
    // Debug builds go ahead and include a RETURN field and hang onto the
    // typeset for fake returns (e.g. natives).  But they make a note that
    // they are doing this, which helps know what the actual size of the
    // frame would be in a release build (e.g. for a FRM_CELL() assert)
    //
    if (flags & MKF_FAKE_RETURN) {
        header_bits |= ACTION_FLAG_RETURN_DEBUG;
        flags &= ~MKF_FAKE_RETURN;
        assert(not (flags & MKF_RETURN));
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
    DS_PUSH_TRASH; // paramlist[0] will become ACT_ARCHETYPE()
    Init_Unreadable_Blank(DS_TOP);
    DS_PUSH(EMPTY_BLOCK); // param_types[0] (to be OBJECT! canon value, if any)
    DS_PUSH(EMPTY_STRING); // param_notes[0] (holds description, then canon)

    REBOOL has_description = FALSE;
    REBOOL has_types = FALSE;
    REBOOL has_notes = FALSE;

    enum Reb_Spec_Mode mode = SPEC_MODE_NORMAL;

    REBOOL refinement_seen = FALSE;

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

            if (IS_TYPESET(DS_TOP))
                DS_PUSH(EMPTY_BLOCK); // need a block to be in position

            if (IS_BLOCK(DS_TOP)) { // we're in right spot to push notes/title
                DS_PUSH_TRASH;
                Init_Text(DS_TOP, Copy_String_At_Len(item, -1));
            }
            else { // !!! A string was already pushed.  Should we append?
                assert(IS_TEXT(DS_TOP));
                Init_Text(DS_TOP, Copy_String_At_Len(item, -1));
            }

            if (DS_TOP == DS_AT(dsp_orig + 3))
                has_description = TRUE;
            else
                has_notes = TRUE;

            continue;
        }

    //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. /////////////////=//

        if (IS_TAG(item) and (flags & MKF_KEYWORDS)) {
            if (0 == Compare_String_Vals(item, Root_With_Tag, TRUE)) {
                mode = SPEC_MODE_WITH;
            }
            else if (0 == Compare_String_Vals(item, Root_Local_Tag, TRUE)) {
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
                assert(IS_TEXT(DS_TOP)); // !!! are blocks after notes good?

                if (IS_BLANK_RAW(DS_TOP - 2)) {
                    //
                    // No parameters pushed, e.g. func [[integer!] {<-- bad}]
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
            else if (not IS_WORD(item) and not IS_SET_WORD(item))
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
        assert(IS_TEXT(DS_TOP));

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
                : ALL_64 & ~(FLAGIT_KIND(REB_MAX_VOID) | FLAGIT_KIND(REB_ACTION)),
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
        if (STR_SYMBOL(canon) == SYM_RETURN and not (flags & MKF_LEAVE)) {
            assert(definitional_return_dsp == 0);
            if (IS_SET_WORD(item))
                definitional_return_dsp = DSP; // RETURN: explicitly tolerated
            else
                flags &= ~(MKF_RETURN | MKF_FAKE_RETURN);
        }
        else if (
            STR_SYMBOL(canon) == SYM_LEAVE
            and not (flags & (MKF_RETURN | MKF_FAKE_RETURN))
        ){
            assert(definitional_leave_dsp == 0);
            if (IS_SET_WORD(item))
                definitional_leave_dsp = DSP; // LEAVE: explicitly tolerated
            else
                flags &= ~MKF_LEAVE;
        }

        if (mode == SPEC_MODE_WITH and not IS_SET_WORD(item)) {
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

    // Go ahead and flesh out the TYPESET! BLOCK! STRING! triples.
    //
    if (IS_TYPESET(DS_TOP))
        DS_PUSH(EMPTY_BLOCK);
    if (IS_BLOCK(DS_TOP))
        DS_PUSH(EMPTY_STRING);
    assert((DSP - dsp_orig) % 3 == 0); // must be a multiple of 3

    // Definitional RETURN and LEAVE slots must have their argument values
    // fulfilled with ACTION! values specific to the function being called
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
        header_bits |= ACTION_FLAG_LEAVE;
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
                or not (has_description or has_types or has_notes)
                    ? ALL_64
                    : ALL_64 & ~(
                        FLAGIT_KIND(REB_MAX_VOID) | FLAGIT_KIND(REB_ACTION)
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
        header_bits |= ACTION_FLAG_RETURN;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    REBCNT num_slots = (DSP - dsp_orig) / 3;

    // If we pushed a typeset for a return and it's a native, it actually
    // doesn't want a RETURN: key in the frame in release builds.  We'll omit
    // from the copy.
    //
    if (definitional_return_dsp != 0 and (flags & MKF_FAKE_RETURN))
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
    // have a valid facade so CTX_KEYLIST() will work.  The Make_Action()
    // calls that provide facades all currently build the full function before
    // trying to add any meta information that includes frames, so they do
    // not have to do this.
    //
    LINK(paramlist).facade = paramlist;

    if (TRUE) {
        RELVAL *dest = ARR_HEAD(paramlist); // canon function value
        RESET_VAL_HEADER(dest, REB_ACTION);
        SET_VAL_FLAGS(dest, header_bits);
        dest->payload.action.paramlist = paramlist;
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

            if (definitional_return and src == definitional_return)
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
                Move_Value(dest, definitional_return);
                ++dest;
            }
        }

        // Must remove binder indexes for all words, even if about to fail
        //
        src = DS_AT(dsp_orig + 1) + 3;
        for (; src <= DS_TOP; src += 3, ++dest) {
            if (
                Remove_Binder_Index_Else_0(&binder, VAL_PARAM_CANON(src))
                == 0
            ){
                assert(duplicate != NULL);
            }
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

    // !!! See notes on ACTION-META in %sysobj.r

    REBCTX *meta = NULL;

    if (has_description or has_types or has_notes) {
        meta = Copy_Context_Shallow(VAL_CONTEXT(Root_Action_Meta));
        MANAGE_ARRAY(CTX_VARLIST(meta));
    }

    MISC(paramlist).meta = meta;

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
            num_slots, ARRAY_FLAG_VARLIST
        );
        MISC(types_varlist).meta = NULL; // GC sees this, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(types_varlist), paramlist);

        REBVAL *dest = SINK(ARR_HEAD(types_varlist)); // "rootvar"
        RESET_VAL_HEADER(dest, REB_FRAME);
        dest->payload.any_context.varlist = types_varlist; // canon FRAME!
        dest->payload.any_context.phase = ACT(paramlist);
        INIT_BINDING(dest, UNBOUND);

        ++dest;

        REBVAL *src = DS_AT(dsp_orig + 2);
        src += 3;
        for (; src <= DS_TOP; src += 3) {
            assert(IS_BLOCK(src));
            if (definitional_return and src == definitional_return + 1)
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
            if (VAL_ARRAY_LEN_AT(definitional_return + 1) != 0) {
                Move_Value(
                    CTX_VAR(meta, STD_ACTION_META_RETURN_TYPE),
                    &definitional_return[1]
                );
            }

            if (not (flags & MKF_FAKE_RETURN)) {
                Init_Void(dest); // clear the local RETURN: var's description
                ++dest;
            }
        }

        TERM_ARRAY_LEN(types_varlist, num_slots);
        MANAGE_ARRAY(types_varlist);

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
            num_slots, ARRAY_FLAG_VARLIST
        );
        MISC(notes_varlist).meta = NULL; // GC sees this, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(notes_varlist), paramlist);

        REBVAL *dest = SINK(ARR_HEAD(notes_varlist)); // "rootvar"
        RESET_VAL_HEADER(dest, REB_FRAME);
        dest->payload.any_context.varlist = notes_varlist; // canon FRAME!
        dest->payload.any_context.phase = ACT(paramlist);
        INIT_BINDING(dest, UNBOUND);

        ++dest;

        REBVAL *src = DS_AT(dsp_orig + 3);
        src += 3;
        for (; src <= DS_TOP; src += 3) {
            assert(IS_TEXT(src));
            if (definitional_return and src == definitional_return + 2)
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
                Init_Void(CTX_VAR(meta, STD_ACTION_META_RETURN_NOTE));
            else {
                Move_Value(
                    CTX_VAR(meta, STD_ACTION_META_RETURN_NOTE),
                    &definitional_return[2]
                );
            }

            if (not (flags & MKF_FAKE_RETURN)) {
                Init_Void(dest);
                ++dest;
            }
        }

        TERM_ARRAY_LEN(notes_varlist, num_slots);
        MANAGE_ARRAY(notes_varlist);

        Init_Any_Context(
            CTX_VAR(meta, STD_ACTION_META_PARAMETER_NOTES),
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
// dispatcher that will be called by Do_Core.  Dispatchers are of the form:
//
//     REB_R Dispatcher(REBFRM *f) {...}
//
// The REBACT returned is "archetypal" because individual REBVALs which hold
// the same REBACT may differ in a per-REBVAL "binding".  (This is how one
// RETURN is distinguished from another--the binding data stored in the REBVAL
// identifies the pointer of the FRAME! to exit).
//
// Actions have an associated REBVAL-sized cell of data, accessible via
// ACT_BODY().  This is where they can store information that will be
// available when the dispatcher is called.  Despite being called "body", it
// doesn't have to be an array--it can be any REBVAL.
//
REBACT *Make_Action(
    REBARR *paramlist,
    REBNAT dispatcher, // native C function called by Do_Core
    REBARR *opt_facade, // if provided, 0 element must be underlying function
    REBCTX *opt_exemplar // if provided, should be consistent w/next level
){
    ASSERT_ARRAY_MANAGED(paramlist);

    RELVAL *rootparam = ARR_HEAD(paramlist);
    assert(IS_ACTION(rootparam)); // !!! body not fully formed...
    assert(rootparam->payload.action.paramlist == paramlist);
    assert(VAL_BINDING(rootparam) == UNBOUND); // archetype

    // Precalculate cached function flags.
    //
    // Note: ACTION_FLAG_DEFERS_LOOKBACK is only relevant for un-refined-calls.
    // No lookback function calls trigger from PATH!.  HOWEVER: specialization
    // does come into play because it may change what the first "real"
    // argument is.  But again, we're only interested in specialization's
    // removal of *non-refinement* arguments.

    REBOOL first_arg = TRUE;

    REBVAL *param = KNOWN(rootparam) + 1;
    for (; NOT_END(param); ++param) {
        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_LOCAL:
            break; // skip

        case PARAM_CLASS_RETURN: {
            assert(VAL_PARAM_SYM(param) == SYM_RETURN);

            // See notes on ACTION_FLAG_INVISIBLE.
            //
            if (VAL_TYPESET_BITS(param) == 0)
                SET_VAL_FLAG(rootparam, ACTION_FLAG_INVISIBLE);
            break; }

        case PARAM_CLASS_LEAVE: {
            assert(VAL_PARAM_SYM(param) == SYM_LEAVE);
            break; } // skip.

        case PARAM_CLASS_REFINEMENT:
            //
            // hit before hitting any basic args, so not a brancher, and not
            // a candidate for deferring lookback arguments.
            //
            first_arg = FALSE;
            break;

        case PARAM_CLASS_NORMAL:
            //
            // First argument is not tight, and not specialized, so cache flag
            // to report that fact.
            //
            if (first_arg and NOT_VAL_FLAG(param, TYPESET_FLAG_HIDDEN)) {
                SET_VAL_FLAG(rootparam, ACTION_FLAG_DEFERS_LOOKBACK);
                first_arg = FALSE;
            }
            break;

        // Otherwise, at least one argument but not one that requires the
        // deferring of lookback.

        case PARAM_CLASS_TIGHT:
            //
            // If first argument is tight, and not specialized, no flag needed
            //
            if (first_arg and NOT_VAL_FLAG(param, TYPESET_FLAG_HIDDEN))
                first_arg = FALSE;
            break;

        case PARAM_CLASS_HARD_QUOTE:
            if (TYPE_CHECK(param, REB_MAX_VOID))
                fail ("Hard quoted function parameters cannot receive voids");

            goto quote_check; // avoid implicit fallthrough warning

        case PARAM_CLASS_SOFT_QUOTE:

        quote_check:;

            if (first_arg and NOT_VAL_FLAG(param, TYPESET_FLAG_HIDDEN)) {
                SET_VAL_FLAG(rootparam, ACTION_FLAG_QUOTES_FIRST_ARG);
                first_arg = FALSE;
            }
            break;

        default:
            assert(FALSE);
        }
    }

    // The "body" for a function can be any REBVAL.  It doesn't have to be
    // a block--it's anything that the dispatcher might wish to interpret.

    REBARR *body_holder = Alloc_Singular_Array();
    Init_Blank(ARR_SINGLE(body_holder));
    MANAGE_ARRAY(body_holder);

    rootparam->payload.action.body_holder = body_holder;

    // The C function pointer is stored inside the REBSER node for the body.
    // Hence there's no need for a `switch` on a function class in Do_Core,
    // Having a level of indirection from the REBVAL bits themself also
    // facilitates the "Hijacker" to change multiple REBVALs behavior.

    MISC(body_holder).dispatcher = dispatcher;

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
        assert(LINK(paramlist).facade == paramlist);
    }
    else
        LINK(paramlist).facade = opt_facade;

    if (opt_exemplar == NULL) {
        //
        // !!! There may be some efficiency hack where this could be END, so
        // that when a REBFRM's ->special field is set there's no need to
        // check for NULL.
        //
        LINK(body_holder).exemplar = NULL;
    }
    else {
        // Because a dispatcher can update the phase and swap in the next
        // function with R_REDO_XXX, consistency checking isn't easily
        // done on whether the exemplar is "compatible" (and there may be
        // dispatcher forms which intentionally muck with the exemplar to
        // be incompatible, but these don't exist yet.)  So just check it's
        // compatible with the underlying frame.
        //
        // Base it off the facade since ACT_NUM_PARAMS(ACT_UNDERLYING())
        // would assert, since the function we're making is incomplete..
        //
        assert(
            CTX_LEN(opt_exemplar) == ARR_LEN(LINK(paramlist).facade) - 1
        );

        LINK(body_holder).exemplar = opt_exemplar;
    }

    // The meta information may already be initialized, since the native
    // version of paramlist construction sets up the FUNCTION-META information
    // used by HELP.  If so, it must be a valid REBCTX*.  Otherwise NULL.
    //
    assert(
        MISC(paramlist).meta == NULL
        or GET_SER_FLAG(CTX_VARLIST(MISC(paramlist).meta), ARRAY_FLAG_VARLIST)
    );

    // Note: used to set the keys of natives as read-only so that the debugger
    // couldn't manipulate the values in a native frame out from under it,
    // potentially crashing C code (vs. just causing userspace code to
    // error).  That protection is now done to the frame series on reification
    // in order to be able to MAKE FRAME! and reuse the native's paramlist.

    assert(NOT_SER_FLAG(paramlist, ARRAY_FLAG_FILE_LINE));
    assert(NOT_SER_FLAG(body_holder, ARRAY_FLAG_FILE_LINE));

    return ACT(paramlist);
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
    REBARR *varlist = Alloc_Singular_Array_Core(
        ARRAY_FLAG_VARLIST | CONTEXT_FLAG_STACK
    );
    MISC(varlist).meta = NULL;
    MANAGE_ARRAY(varlist);

    RELVAL *rootvar = ARR_SINGLE(varlist);
    RESET_VAL_HEADER(rootvar, REB_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = a;
    INIT_BINDING(rootvar, UNBOUND); // !!! is a binding relevant?

    REBCTX *expired = CTX(varlist);
    SET_SER_INFO(varlist, SERIES_INFO_INACCESSIBLE);
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
        a = VAL_ACTION(ACT_BODY(a));
        // !!! Review what should happen to binding
    }

    // !!! Should the binding make a difference in the returned body?  It is
    // exposed programmatically via CONTEXT OF.
    //
    UNUSED(binding);

    if (
        ACT_DISPATCHER(a) == &Noop_Dispatcher
        or ACT_DISPATCHER(a) == &Unchecked_Dispatcher
        or ACT_DISPATCHER(a) == &Voider_Dispatcher
        or ACT_DISPATCHER(a) == &Returner_Dispatcher
        or ACT_DISPATCHER(a) == &Block_Dispatcher
    ){
        // Interpreted code, the body is a block with some bindings relative
        // to the action.

        // The ACTION_FLAG_LEAVE/ACTION_FLAG_RETURN tricks for definitional
        // scoping make it seem like a generator authored more code in the
        // action's body...but the code isn't *actually* there and an
        // optimized internal trick is used.  Fake the code if needed.

        REBVAL *example;
        REBCNT real_body_index;
        if (GET_ACT_FLAG(a, ACTION_FLAG_RETURN)) {
            assert(not GET_ACT_FLAG(a, ACTION_FLAG_LEAVE)); // can't have both
            example = Get_System(SYS_STANDARD, STD_FUNC_BODY);
            real_body_index = 4;
        }
        else if (GET_ACT_FLAG(a, ACTION_FLAG_LEAVE)) {
            example = Get_System(SYS_STANDARD, STD_PROC_BODY);
            real_body_index = 4;
        }
        else {
            example = NULL;
            real_body_index = 0; // avoid compiler warning
            UNUSED(real_body_index);
        }

        REBARR *real_body = VAL_ARRAY(ACT_BODY(a));
        assert(GET_SER_INFO(real_body, SERIES_INFO_FROZEN));

        REBARR *maybe_fake_body;
        if (example == NULL) {
            maybe_fake_body = real_body;
            assert(GET_SER_INFO(maybe_fake_body, SERIES_INFO_FROZEN));
        }
        else {
            // See %sysobj.r for STANDARD/FUNC-BODY and STANDARD/PROC-BODY
            //
            maybe_fake_body = Copy_Array_Shallow(
                VAL_ARRAY(example),
                VAL_SPECIFIER(example)
            );
            SET_SER_INFO(maybe_fake_body, SERIES_INFO_FROZEN);

            // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.
            // To give it the appearance of executing code in place, we use
            // a GROUP!.

            RELVAL *slot = ARR_AT(maybe_fake_body, real_body_index); // #BODY
            assert(IS_ISSUE(slot));

            RESET_VAL_HEADER_EXTRA(slot, REB_GROUP, 0); // clear VAL_FLAG_LINE
            INIT_VAL_ARRAY(slot, VAL_ARRAY(ACT_BODY(a)));
            VAL_INDEX(slot) = 0;
            INIT_BINDING(slot, a); // relative binding

            MANAGE_ARRAY(maybe_fake_body);
        }

        // Cannot give user a relative value back, so make the relative
        // body specific to a fabricated expired frame.  See #2221

        RESET_VAL_HEADER_EXTRA(out, REB_BLOCK, 0);
        INIT_VAL_ARRAY(out, maybe_fake_body);
        VAL_INDEX(out) = 0;
        INIT_BINDING(out, Make_Expired_Frame_Ctx_Managed(a));
        return;
    }

    if (ACT_DISPATCHER(a) == &Specializer_Dispatcher) {
        //
        // The FRAME! stored in the body for the specialization has a phase
        // which is actually the function to be run.
        //
        assert(IS_FRAME(ACT_BODY(a)));
        Move_Value(out, KNOWN(ACT_BODY(a)));
        return;
    }

    if (ACT_DISPATCHER(a) == &Type_Action_Dispatcher) {
        assert(IS_WORD(ACT_BODY(a)));
        Move_Value(out, KNOWN(ACT_BODY(a)));
        return;
    }

    Init_Blank(out); // natives, ffi routines, etc.
    return;
}


//
//  Make_Interpreted_Action_May_Fail: C
//
// This is the support routine behind `MAKE ACTION!`, FUNC, and PROC.
//
// Ren-C's schematic for the FUNC and PROC generators is *very* different
// from R3-Alpha, whose definition of FUNC was simply:
//
//     make function! copy/deep reduce [spec body]
//
// Ren-C's `make action!` doesn't need to copy the spec (it does not save
// it--parameter descriptions are in a meta object).  It also copies the body
// by virtue of the need to relativize it.  They also have "definitional
// return" constructs so that the body introduces RETURN and LEAVE constructs
// specific to each action invocation, so the body acts more like:
//
//     return: make action! [
//         [{Returns a value from a function.} value [<opt> any-value!]]
//         [unwind/with (context of 'return) :value]
//     ]
//     (body goes here)
//
// This pattern addresses "Definitional Return" in a way that does not
// technically require building RETURN or LEAVE in as a language keyword in
// any specific form (in the sense that MAKE ACTION! does not itself
// require it, and one can pretend FUNC and PROC don't exist).
//
// FUNC and PROC optimize by not internally building or executing the
// equivalent body, but giving it back from BODY-OF.  This is another benefit
// of making a copy--since the user cannot access the new root, it makes it
// possible to "lie" about what the body "above" is.  This gives FUNC and PROC
// the edge to pretend to add containing code and simulate its effects, while
// really only holding onto the body the caller provided.
//
// While plain MAKE ACTION! has no RETURN, UNWIND can be used to exit frames
// but must be explicit about what frame is being exited.  This can be used
// by usermode generators that want to create something return-like.
//
REBACT *Make_Interpreted_Action_May_Fail(
    const REBVAL *spec,
    const REBVAL *code,
    REBFLGS mkf_flags // MKF_RETURN, MKF_LEAVE, etc.
) {
    assert(IS_BLOCK(spec) and IS_BLOCK(code));

    REBACT *a = Make_Action(
        Make_Paramlist_Managed_May_Fail(spec, mkf_flags),
        &Noop_Dispatcher, // will be overwritten if non-NULL body
        NULL, // no facade (use paramlist)
        NULL // no specialization exemplar (or inherited exemplar)
    );

    // We look at the *actual* function flags; e.g. the person may have used
    // the FUNC generator (with MKF_RETURN) but then named a parameter RETURN
    // which overrides it, so the value won't have ACTION_FLAG_RETURN.
    //
    REBVAL *value = ACT_ARCHETYPE(a);

    REBARR *copy;
    if (VAL_ARRAY_LEN_AT(code) == 0) { // optimize empty body case

        if (GET_VAL_FLAG(value, ACTION_FLAG_INVISIBLE)) {
            ACT_DISPATCHER(a) = &Commenter_Dispatcher;
        }
        else if (GET_VAL_FLAG(value, ACTION_FLAG_RETURN)) {
            REBVAL *typeset = ACT_PARAM(a, ACT_NUM_PARAMS(a));
            assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);
            if (not TYPE_CHECK(typeset, REB_MAX_VOID)) // all do [] can return
                ACT_DISPATCHER(a) = &Returner_Dispatcher; // error when run
        }
        else {
            // Keep the Noop_Dispatcher passed in above
        }

        // Reusing EMPTY_ARRAY won't allow adding ARRAY_FLAG_FILE_LINE bits
        //
        copy = Make_Array_Core(1, NODE_FLAG_MANAGED);
    }
    else { // body not empty, pick dispatcher based on output disposition

        if (GET_VAL_FLAG(value, ACTION_FLAG_INVISIBLE))
            ACT_DISPATCHER(a) = &Elider_Dispatcher; // no f->out mutation
        else if (GET_VAL_FLAG(value, ACTION_FLAG_RETURN))
            ACT_DISPATCHER(a) = &Returner_Dispatcher; // type checks f->out
        else if (GET_VAL_FLAG(value, ACTION_FLAG_LEAVE))
            ACT_DISPATCHER(a) = &Voider_Dispatcher; // forces f->out void
        else
            ACT_DISPATCHER(a) = &Unchecked_Dispatcher; // unchecked f->out

        copy = Copy_And_Bind_Relative_Deep_Managed(
            code, // new copy has locals bound relatively to the new action
            ACT_PARAMLIST(a),
            TS_ANY_WORD
        );
    }

    RELVAL *body = ACT_BODY(a);
    RESET_VAL_HEADER(body, REB_BLOCK); // Init_Block() assumes specific values
    INIT_VAL_ARRAY(body, copy);
    VAL_INDEX(body) = 0;
    INIT_BINDING(body, a); // Record that block is relative to a function

    // Favor the spec first, then the body, for file and line information.
    //
    if (GET_SER_FLAG(VAL_ARRAY(spec), ARRAY_FLAG_FILE_LINE)) {
        LINK(copy).file = LINK(VAL_ARRAY(spec)).file;
        MISC(copy).line = MISC(VAL_ARRAY(spec)).line;
        SET_SER_FLAG(copy, ARRAY_FLAG_FILE_LINE);
    }
    else if (GET_SER_FLAG(VAL_ARRAY(code), ARRAY_FLAG_FILE_LINE)) {
        LINK(copy).file = LINK(VAL_ARRAY(code)).file;
        MISC(copy).line = MISC(VAL_ARRAY(code)).line;
        SET_SER_FLAG(copy, ARRAY_FLAG_FILE_LINE);
    }
    else {
        // Ideally all source series should have a file and line numbering
        // At the moment, if a function is created in the body of another
        // function it doesn't work...trying to fix that.
    }

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
    if (not LEGACY(OPTIONS_UNLOCKED_SOURCE))
        Deep_Freeze_Array(VAL_ARRAY(body));
  #endif

    return a;
}


//
//  Make_Frame_For_Action: C
//
// This creates a *non-stack-allocated* FRAME!, which can be used in function
// applications or specializations.  It reuses the keylist of the function
// but makes a new varlist.
//
void Make_Frame_For_Action(
    REBVAL *out,
    const REBVAL *action // need the binding, can't just be a REBACT*
){
    REBACT *a = VAL_ACTION(action);
    REBCTX *exemplar = ACT_EXEMPLAR(a); // may be NULL

    REBCNT facade_len = ACT_FACADE_NUM_PARAMS(a) + 1;
    REBARR *varlist = Make_Array_Core(
        facade_len, // +1 for the CTX_ARCHETYPE() at [0]
        ARRAY_FLAG_VARLIST | SERIES_FLAG_FIXED_SIZE
    );

    REBVAL *rootvar = SINK(ARR_HEAD(varlist));
    RESET_VAL_HEADER(rootvar, REB_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = a;
    INIT_BINDING(rootvar, UNBOUND);

    REBVAL *arg = rootvar + 1;
    REBVAL *param = ACT_FACADE_HEAD(a);

    if (exemplar == NULL) {
        //
        // No prior specialization means all the slots should be void.
        //
        for (; NOT_END(param); ++param, ++arg)
            Init_Void(arg);
    }
    else {
        // Partially specialized refinements put INTEGER! in refinement slots
        // (see notes on REB_0_PARTIAL for the mechanic).  But we don't want
        // to leak that to the user.  Convert to TRUE or void as appropriate,
        // so FRAME! won't show these refinements.
        //
        // !!! This loses the ordering, see Make_Frame_For_Specialization for
        // a frame-making mechanic which preserves it.
        //
        // !!! Logic is duplicated in APPLY with the slight change of needing
        // to prep stack cells; review.
        //
        REBVAL *special = CTX_VARS_HEAD(exemplar);
        for (; NOT_END(param); ++param, ++arg, ++special) {
            if (VAL_PARAM_CLASS(param) != PARAM_CLASS_REFINEMENT) {
                Move_Value(arg, special);
                continue;
            }
            if (IS_LOGIC(special)) { // fully specialized, or disabled
                Init_Logic(arg, VAL_LOGIC(special));
                continue;
            }

            // See %c-special.c for an overview of why a REFINEMENT! in an
            // exemplar slot and void have a complex interpretation.
            //
            // Drive whether the refinement is present or not based on whether
            // it's available for the user to pass in or not.
            //
            assert(IS_REFINEMENT(special) or IS_VOID(special));
            if (IS_REFINEMENT_SPECIALIZED(param))
                Init_Logic(arg, TRUE);
            else
                Init_Void(arg);
        }
    }

    TERM_ARRAY_LEN(varlist, facade_len);

    MISC(varlist).meta = NULL; // GC sees this, we must initialize

    // The facade of the action is used as the keylist of the frame, as
    // that is how many values the frame must ultimately have.  Since this
    // is not a stack frame, there will be no ->phase to override it...the
    // FRAME! will always be viewed with those keys.
    //
    // Also, for things like definitional RETURN and LEAVE we had to stow the
    // `binding` field in the FRAME! REBVAL, since the single archetype
    // paramlist does not hold enough information to know where to return to.
    //
    // Note that this precludes the LINK().keysource from holding a REBFRM*,
    // since it is holding a parameter list instead.
    //
    INIT_CTX_KEYLIST_SHARED(CTX(varlist), ACT_FACADE(a));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(CTX(varlist)));

    Init_Any_Context(out, REB_FRAME, CTX(varlist));
    INIT_BINDING(out, VAL_BINDING(action));
    out->payload.any_context.phase = a;
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
//  Type_Action_Dispatcher: C
//
// A "type action" is what R3-Alpha/Rebol2 had just called "actions" (until
// Ren-C took that as the umbrella term for all "invokables").  This kind of
// dispatch is based on the first argument's type, with the idea being a
// single C function for the type has a switch() statement in it and can
// handle many different such actions for that type.
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
//
// Note: R3-Alpha had an enum type for "action numbers", so that they could be
// handled in a switch statement.  Ren-C just unified that with the REBSYM
// number mechanism, so it leverages words that have compile-time constants
// (like SYM_APPEND, etc.)  Using a symbol in this way is called a "verb".
//
REB_R Type_Action_Dispatcher(REBFRM *f)
{
    enum Reb_Kind kind = VAL_TYPE(FRM_ARG(f, 1));
    REBSYM verb = VAL_WORD_SYM(ACT_BODY(f->phase));
    assert(verb != SYM_0);

    // !!! Some reflectors are more general and apply to all types (e.g. TYPE)
    // while others only apply to some types (e.g. LENGTH or HEAD only to
    // series, or perhaps things like PORT! that wish to act like a series).
    // This suggests a need for a kind of hierarchy of handling.
    //
    // The series common code is in Series_Common_Action_Maybe_Unhandled(),
    // but that is only called from series.  Handle a few extra cases here.
    //
    if (verb == SYM_REFLECT) {
        REBFRM *frame_ = f;
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value));
        REBSYM property = VAL_WORD_SYM(ARG(property));

        switch (property) {
        case SYM_0:
            //
            // If a word wasn't in %words.r, it has no integer SYM.  There is
            // no way for a built-in reflector to handle it...since they just
            // operate on SYMs in a switch().  Longer term, a more extensible
            // idea may be necessary.
            //
            fail (Error_Cannot_Reflect(kind, ARG(property)));

        case SYM_TYPE:
            if (kind == REB_MAX_VOID)
                return R_VOID; // `() = type of ()`, `null = type of ()`
            Init_Datatype(f->out, kind);
            return R_OUT;

        default:
            // !!! Are there any other universal reflectors?
            break;
        }
    }

    // !!! The reflector for TYPE is universal and so it is allowed on voids,
    // but in general actions should not allow void first arguments...there's
    // no entry in the dispatcher table for them.
    //
    if (kind == REB_MAX_VOID)
        fail ("VOID isn't valid for REFLECT, except for TYPE OF ()");

    assert(kind < REB_MAX);

    REBTAF subdispatch = Value_Dispatch[kind];
    return subdispatch(f, verb);
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
    assert(VAL_LEN_AT(ACT_BODY(f->phase)) == 0);
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
    RELVAL *datatype = ACT_BODY(f->phase);
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
    RELVAL *typeset = ACT_BODY(f->phase);
    assert(IS_TYPESET(typeset));
    if (TYPE_CHECK(typeset, VAL_TYPE(FRM_ARG(f, 1))))
        return R_TRUE;
    return R_FALSE;
}


//
//  Unchecked_Dispatcher: C
//
// This is the default MAKE ACTION! dispatcher for interpreted functions
// (whose body is a block that runs through DO []).  There is no return type
// checking done on these simple functions.
//
REB_R Unchecked_Dispatcher(REBFRM *f)
{
    RELVAL *body = ACT_BODY(f->phase);
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    if (Do_At_Throws(f->out, VAL_ARRAY(body), 0, SPC(f)))
        return R_OUT_IS_THROWN;

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
    RELVAL *body = ACT_BODY(f->phase);
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    if (Do_At_Throws(f->out, VAL_ARRAY(body), 0, SPC(f)))
        return R_OUT_IS_THROWN;

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
    RELVAL *body = ACT_BODY(f->phase);
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    if (Do_At_Throws(f->out, VAL_ARRAY(body), 0, SPC(f)))
        return R_OUT_IS_THROWN;

    REBVAL *typeset = ACT_PARAM(f->phase, ACT_NUM_PARAMS(f->phase));
    assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);

    // Typeset bits for locals in frames are usually ignored, but the RETURN:
    // local uses them for the return types of a "virtual" definitional return
    // if the parameter is PARAM_CLASS_RETURN.
    //
    if (not TYPE_CHECK(typeset, VAL_TYPE(f->out)))
        fail (Error_Bad_Return_Type(f, VAL_TYPE(f->out)));

    return R_OUT;
}


//
//  Elider_Dispatcher: C
//
// This is used by "invisible" functions (who in their spec say `return: []`).
// The goal is to evaluate a function call in such a way that its presence
// doesn't disrupt the chain of evaluation any more than if the call were not
// there.  (The call can have side effects, however.)
//
REB_R Elider_Dispatcher(REBFRM *f)
{
    RELVAL *body = ACT_BODY(f->phase);
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    // !!! It would be nice to use the frame's spare "cell" for the thrownaway
    // result, but Fetch_Next code expects to use the cell.
    //
    DECLARE_LOCAL (dummy);
    SET_END(dummy);

    if (Do_At_Throws(dummy, VAL_ARRAY(body), 0, SPC(f))) {
        Move_Value(f->out, dummy);
        return R_OUT_IS_THROWN;
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
    assert(VAL_LEN_AT(ACT_BODY(f->phase)) == 0);
    UNUSED(f);
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
    RELVAL *hijacker = ACT_BODY(f->phase);

    // We need to build a new frame compatible with the hijacker, and
    // transform the parameters we've gathered to be compatible with it.
    //
    if (Redo_Action_Throws(f, VAL_ACTION(hijacker)))
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
    RELVAL *adaptation = ACT_BODY(f->phase);
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
    if (Do_At_Throws(f->out, VAL_ARRAY(prelude), VAL_INDEX(prelude), SPC(f)))
        return R_OUT_IS_THROWN;

    f->phase = VAL_ACTION(adaptee);
    f->binding = VAL_BINDING(adaptee);
    return R_REDO_CHECKED; // Have Do_Core run the adaptee updated into f->phase
}


//
//  Encloser_Dispatcher: C
//
// Dispatcher used by ENCLOSE.
//
REB_R Encloser_Dispatcher(REBFRM *f)
{
    RELVAL *enclosure = ACT_BODY(f->phase);
    assert(ARR_LEN(VAL_ARRAY(enclosure)) == 2);

    RELVAL* inner = KNOWN(VAL_ARRAY_AT_HEAD(enclosure, 0)); // same args as f
    assert(IS_ACTION(inner));
    REBVAL* outer = KNOWN(VAL_ARRAY_AT_HEAD(enclosure, 1)); // 1 FRAME! arg
    assert(IS_ACTION(outer));

    // We want to call OUTER with a FRAME! value that will dispatch to INNER
    // when it runs DO on it.  The contents of the arguments for that call to
    // inner should start out as the same as what has been built for the
    // passed in F.  (OUTER may mutate these before the call if it likes.)
    //
    // !!! It is desirable in the general case to just reuse the values in
    // the chunk stack that f already has for inner.  However, inner is going
    // to be called at a deeper stack level than outer.  This tampers with
    // the logic of the system for things like Move_Value(), which have to
    // make decisions about the relative lifetimes of cells in order to
    // decide whether to reify things (like REBFRM* to a REBSER* for FRAME!)
    //
    // !!! To get the ball rolling with testing the feature, pass a copy of
    // the frame values in a heap-allocated FRAME!...which it will turn around
    // and stack allocate again when DO is called.  That's triply inefficient
    // because it forces reification of the stub frame just to copy it...
    // which is not necessary, but easier code to write since it can use
    // Copy_Context_Core().  Tune this all up as it becomes more mainstream,
    // since you don't need to make 1 copy of the values...much less 2.

    const REBU64 types = 0;
    REBCTX *copy = Copy_Context_Core(
        Context_For_Frame_May_Reify_Managed(f), types
    );

    DECLARE_LOCAL (arg);
    Init_Any_Context(arg, REB_FRAME, copy);

    // !!! Review how exactly this update to the phase and binding is supposed
    // to work.  We know that when `outer` tries to DO its frame argument,
    // it needs to run inner with the correct binding.
    //
    arg->payload.any_context.phase = VAL_ACTION(inner);
    INIT_BINDING(arg, VAL_BINDING(inner));

    const REBOOL fully = TRUE;
    if (Apply_Only_Throws(f->out, fully, outer, arg, END))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  Chainer_Dispatcher: C
//
// Dispatcher used by CHAIN.
//
REB_R Chainer_Dispatcher(REBFRM *f)
{
    REBVAL *pipeline = KNOWN(ACT_BODY(f->phase)); // array of functions

    // Before skipping off to find the underlying non-chained function
    // to kick off the execution, the post-processing pipeline has to
    // be "pushed" so it is not forgotten.  Go in reverse order so
    // the function to apply last is at the bottom of the stack.
    //
    REBVAL *value = KNOWN(ARR_LAST(VAL_ARRAY(pipeline)));
    while (value != VAL_ARRAY_HEAD(pipeline)) {
        assert(IS_ACTION(value));
        DS_PUSH(KNOWN(value));
        --value;
    }

    // Extract the first function, itself which might be a chain.
    //
    f->phase = VAL_ACTION(value);
    f->binding = VAL_BINDING(value);

    return R_REDO_UNCHECKED; // signatures should match
}


//
//  Get_If_Word_Or_Path_Throws: C
//
// Some routines like APPLY and SPECIALIZE are willing to take a WORD! or
// PATH! instead of just the value type they are looking for, and perform
// the GET for you.  By doing the GET inside the function, they are able
// to preserve the symbol:
//
//     >> apply 'append [value: 'c]
//     ** Script error: append is missing its series argument
//
// If push_refinements is used, then it avoids intermediate specializations...
// e.g. `specialize 'append/dup [part: true]` can be done with one FRAME!.
//
REBOOL Get_If_Word_Or_Path_Throws(
    REBVAL *out,
    REBSTR **opt_name_out,
    const RELVAL *v,
    REBSPC *specifier,
    REBOOL push_refinements
) {
    if (IS_WORD(v)) {
        *opt_name_out = VAL_WORD_SPELLING(v);
        Move_Opt_Var_May_Fail(out, v, specifier);
    }
    else if (IS_PATH(v)) {
        REBSPC *derived = Derive_Specifier(specifier, v);
        if (Do_Path_Throws_Core(
            out,
            opt_name_out, // requesting says we run functions (not GET-PATH!)
            REB_PATH,
            VAL_ARRAY(v),
            VAL_INDEX(v),
            derived,
            NULL, // `setval`: null means don't treat as SET-PATH!
            push_refinements
                ? DO_FLAG_PUSH_PATH_REFINEMENTS // pushed in reverse order
                : DO_MASK_NONE
        )){
            return TRUE;
        }
    }
    else {
        *opt_name_out = NULL;
        Derelativize(out, v, specifier);
    }

    return FALSE;
}

