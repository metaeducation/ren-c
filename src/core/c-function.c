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
//  Make_Action_Words_Arr: C
//
// Returns array of function words, unbound.
//
REBARR *Make_Action_Words_Arr(REBACT *act, bool locals)
{
    REBDSP dsp_orig = DSP;

    REBVAL *param = ACT_PARAMS_HEAD(act);
    for (; NOT_END(param); param++) {
        if (Is_Param_Hidden(param)) // specialization hides parameters
            continue;

        enum Reb_Kind kind;
        bool quoted = false;

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
            kind = REB_WORD;
            quoted = true;
            break;

          case PARAM_CLASS_LOCAL:
          case PARAM_CLASS_RETURN: // "magic" local - prefilled invisibly
            if (not locals)
                continue; // treat as invisible, e.g. for WORDS-OF

            kind = REB_SET_WORD;
            break;

          default:
            assert(false);
            DEAD_END;
        }

        Init_Any_Word(DS_PUSH(), kind, VAL_PARAM_SPELLING(param));
        if (quoted)
            Quotify(DS_TOP, 1);
    }

    return Pop_Stack_Values(dsp_orig);
}


//
//  List_Func_Typesets: C
//
// Return a block of function arg typesets.
// Note: skips 0th entry.
//
REBARR *List_Func_Typesets(REBVAL *func)
{
    REBARR *array = Make_Arr(VAL_ACT_NUM_PARAMS(func));
    REBVAL *typeset = VAL_ACT_PARAMS_HEAD(func);

    for (; NOT_END(typeset); typeset++) {
        assert(IS_TYPESET(typeset));

        REBVAL *value = Move_Value(Alloc_Tail_Array(array), typeset);

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
    assert(IS_BLOCK(spec));

    uintptr_t header_bits = 0;

  #if !defined(NDEBUG)
    //
    // Debug builds go ahead and include a RETURN field and hang onto the
    // typeset for fake returns (e.g. natives).
    //
    if (flags & MKF_FAKE_RETURN) {
        flags &= ~MKF_FAKE_RETURN;
        assert(not (flags & MKF_RETURN));
        flags |= MKF_RETURN;
    }
  #endif

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

            if (IS_TYPESET(DS_TOP))
                Move_Value(DS_PUSH(), EMPTY_BLOCK); // need block in position

            if (IS_BLOCK(DS_TOP)) { // we're in right spot to push notes/title
                Init_Text(DS_PUSH(), Copy_String_At_Len(item, -1));
            }
            else { // !!! A string was already pushed.  Should we append?
                assert(IS_TEXT(DS_TOP));
                Init_Text(DS_TOP, Copy_String_At_Len(item, -1));
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
                header_bits |= ACTION_FLAG_VOIDER; // use Voider_Dispatcher()

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
            REBVAL *typeset;
            if (IS_TYPESET(DS_TOP)) {
                REBSPC *derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
                Init_Block(
                    DS_PUSH(),
                    Copy_Array_At_Deep_Managed(
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        derived
                    )
                );

                typeset = DS_TOP - 1; // volatile if you DS_PUSH()!
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
                if (TYPE_CHECK(typeset, REB_MAX_NULLED))
                    fail (Error_Refinement_Arg_Opt_Raw());
            }

            has_types = true;
            continue;
        }

    //=//// ANY-WORD! PARAMETERS THEMSELVES (MAKE TYPESETS w/SYMBOL) //////=//

        enum Reb_Param_Class pclass;
        REBSTR *spelling;
        switch (VAL_TYPE(item)) {
          case REB_WORD:
            spelling = VAL_WORD_SPELLING(item);

            pclass = (mode == SPEC_MODE_LOCAL)
                ? PARAM_CLASS_LOCAL
                : PARAM_CLASS_NORMAL;
            break;

          case REB_GET_WORD:
            if (mode != SPEC_MODE_NORMAL)
                goto mode_mismatch;

            pclass = PARAM_CLASS_HARD_QUOTE;
            spelling = VAL_WORD_SPELLING(item);
            break;

          case REB_QUOTED: {
            const REBCEL *cell = VAL_UNESCAPED(item);
            if (VAL_NUM_QUOTES(item) != 1 or CELL_KIND(cell) != REB_WORD)
                fail ("QUOTED! argument must be a WORD! to soft quote");

            if (mode != SPEC_MODE_NORMAL)
                goto mode_mismatch;

            pclass = PARAM_CLASS_SOFT_QUOTE;
            spelling = VAL_WORD_SPELLING(cell);
            break; }

          case REB_REFINEMENT:

            // !!! If you say [<with> x /foo y] the <with> terminates and a
            // refinement is started.  Same w/<local>.  Is this a good idea?
            // Note that historically, help hides any refinements that appear
            // behind a /local, but this feature has no parallel in Ren-C.
            //
            mode = SPEC_MODE_NORMAL;

            refinement_seen = true;
            pclass = PARAM_CLASS_REFINEMENT;
            spelling = VAL_WORD_SPELLING(item);

            // !!! The typeset bits of a refinement are not currently used.
            // They are checked for TRUE or FALSE but this is done literally
            // by the code.  This means that every refinement has some spare
            // bits available in it for another purpose.
            break;

          case REB_SET_WORD:
            // tolerate as-is if in <local> or <with> mode...
            pclass = PARAM_CLASS_LOCAL;
            spelling = VAL_WORD_SPELLING(item);
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
            if (mode != SPEC_MODE_NORMAL)
                goto mode_mismatch;

            pclass = PARAM_CLASS_TIGHT;
            spelling = VAL_WORD_SPELLING(item);
            break;

          mode_mismatch:
          default:
            fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
        }

        REBSTR *canon = STR_CANON(spelling);
        if (STR_SYMBOL(canon) == SYM_RETURN and pclass != PARAM_CLASS_LOCAL) {
            //
            // Cancel definitional return if any non-SET-WORD! uses the name
            // RETURN when defining a FUNC.
            //
            flags &= ~(MKF_RETURN | MKF_FAKE_RETURN);
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
        if (IS_TYPESET(DS_TOP))
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
        // If the typeset bits contain REB_MAX_NULLED, that indicates <opt>.
        // But Is_Param_Endable() indicates <end>.

        Init_Typeset(
            DS_PUSH(),
            (flags & MKF_ANY_VALUE)
                ? TS_OPT_VALUE
                : TS_VALUE & ~(
                    FLAGIT_KIND(REB_ACTION)
                    | FLAGIT_KIND(REB_VOID)
                ),
            spelling // don't canonize, see #2258
        );
        INIT_VAL_PARAM_CLASS(DS_TOP, pclass);

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
            if (pclass == PARAM_CLASS_LOCAL)
                definitional_return_dsp = DSP; // RETURN: explicitly tolerated
            else
                flags &= ~(MKF_RETURN | MKF_FAKE_RETURN);
        }
    }

    // Go ahead and flesh out the TYPESET! BLOCK! TEXT! triples.
    //
    if (IS_TYPESET(DS_TOP))
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
            Init_Typeset(DS_PUSH(), TS_OPT_VALUE, Canon(SYM_RETURN));
            INIT_VAL_PARAM_CLASS(DS_TOP, PARAM_CLASS_RETURN);
            definitional_return_dsp = DSP;

            Move_Value(DS_PUSH(), EMPTY_BLOCK);
            Move_Value(DS_PUSH(), EMPTY_TEXT);
            // no need to move it--it's already at the tail position
        }
        else {
            REBVAL *param = DS_AT(definitional_return_dsp);
            assert(VAL_PARAM_CLASS(param) == PARAM_CLASS_LOCAL);
            INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_RETURN);

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
    //
    REBARR *paramlist = Make_Arr_Core(num_slots, SERIES_MASK_ACTION);

    if (true) {
        REBVAL *canon = RESET_CELL_EXTRA(
            ARR_HEAD(paramlist),
            REB_ACTION,
            header_bits
        );
        canon->payload.action.paramlist = paramlist;
        INIT_BINDING(canon, UNBOUND);

        REBVAL *dest = canon + 1;

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
            if (not Try_Add_Binder_Index(&binder, VAL_PARAM_CANON(src), 1020))
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
        MANAGE_ARRAY(paramlist);
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
        REBARR *types_varlist = Make_Arr_Core(
            num_slots,
            SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED
        );
        MISC(types_varlist).meta = NULL; // GC sees this, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(types_varlist), paramlist);

        REBVAL *rootvar = RESET_CELL(ARR_HEAD(types_varlist), REB_FRAME);
        rootvar->payload.any_context.varlist = types_varlist; // canon FRAME!
        rootvar->payload.any_context.phase = ACT(paramlist);
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

            if (not (flags & MKF_FAKE_RETURN)) {
                Init_Nulled(dest); // clear the local RETURN: var's description
                ++dest;
            }
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
        REBARR *notes_varlist = Make_Arr_Core(
            num_slots,
            SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED
        );
        MISC(notes_varlist).meta = NULL; // GC sees this, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(notes_varlist), paramlist);

        REBVAL *rootvar = RESET_CELL(ARR_HEAD(notes_varlist), REB_FRAME);
        rootvar->payload.any_context.varlist = notes_varlist; // canon FRAME!
        rootvar->payload.any_context.phase = ACT(paramlist);
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

            if (not (flags & MKF_FAKE_RETURN)) {
                Init_Nulled(dest);
                ++dest;
            }
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
    assert(rootparam->payload.action.paramlist == paramlist);
    assert(rootparam->extra.binding == UNBOUND); // archetype

    // Precalculate cached function flags.
    //
    // Note: ACTION_FLAG_DEFERS_LOOKBACK is only relevant for un-refined-calls.
    // No lookback function calls trigger from PATH!.  HOWEVER: specialization
    // does come into play because it may change what the first "real"
    // argument is.  But again, we're only interested in specialization's
    // removal of *non-refinement* arguments.

    bool first_arg = true;

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

        case PARAM_CLASS_REFINEMENT:
            //
            // hit before hitting any basic args, so not a brancher, and not
            // a candidate for deferring lookback arguments.
            //
            first_arg = false;
            break;

        case PARAM_CLASS_NORMAL:
            //
            // First argument is not tight, and not specialized, so cache flag
            // to report that fact.
            //
            if (first_arg and not Is_Param_Hidden(param)) {
                SET_VAL_FLAG(rootparam, ACTION_FLAG_DEFERS_LOOKBACK);
                first_arg = false;
            }
            break;

        // Otherwise, at least one argument but not one that requires the
        // deferring of lookback.

        case PARAM_CLASS_TIGHT:
            //
            // If first argument is tight, and not specialized, no flag needed
            //
            if (first_arg and not Is_Param_Hidden(param))
                first_arg = false;
            break;

        case PARAM_CLASS_HARD_QUOTE:
            if (TYPE_CHECK(param, REB_MAX_NULLED))
                fail ("Hard quoted function parameters cannot receive nulls");

            goto quote_check;

        case PARAM_CLASS_SOFT_QUOTE:

        quote_check:;

            if (first_arg and not Is_Param_Hidden(param)) {
                SET_VAL_FLAG(rootparam, ACTION_FLAG_QUOTES_FIRST_ARG);
                first_arg = false;
            }
            break;

        default:
            assert(false);
        }
    }

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.  Terminate it
    // at the given length implicitly.

    REBARR *details = Make_Arr_Core(details_capacity, NODE_FLAG_MANAGED);
    TERM_ARRAY_LEN(details, details_capacity);

    rootparam->payload.action.details = details;

    MISC(details).dispatcher = dispatcher; // level of indirection, hijackable

    assert(IS_POINTER_TRASH_DEBUG(LINK(paramlist).trash));

    if (opt_underlying)
        LINK(paramlist).underlying = opt_underlying;
    else {
        // To avoid NULL checking when a function is called and looking for
        // underlying, just use the action's own paramlist if needed.
        //
        LINK(paramlist).underlying = ACT(paramlist);
    }

    if (not opt_exemplar) {
        //
        // No exemplar is used as a cue to set the "specialty" to paramlist,
        // so that Push_Action() can assign f->special directly from it in
        // dispatch, and be equal to f->param.
        //
        LINK(details).specialty = paramlist;
    }
    else {
        // The parameters of the paramlist should line up with the slots of
        // the exemplar (though some of these parameters may be hidden due to
        // specialization, see REB_TS_HIDDEN).
        //
        assert(GET_SER_FLAG(opt_exemplar, NODE_FLAG_MANAGED));
        assert(CTX_LEN(opt_exemplar) == ARR_LEN(paramlist) - 1);

        LINK(details).specialty = CTX_VARLIST(opt_exemplar);
    }

    // The meta information may already be initialized, since the native
    // version of paramlist construction sets up the FUNCTION-META information
    // used by HELP.  If so, it must be a valid REBCTX*.  Otherwise NULL.
    //
    assert(
        not MISC(paramlist).meta
        or GET_SER_FLAG(MISC(paramlist).meta, ARRAY_FLAG_VARLIST)
    );

    assert(NOT_SER_FLAG(paramlist, ARRAY_FLAG_FILE_LINE));
    assert(NOT_SER_FLAG(details, ARRAY_FLAG_FILE_LINE));

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
    // Since passing SERIES_MASK_CONTEXT includes SERIES_FLAG_ALWAYS_DYNAMIC,
    // don't pass it in to the allocation...it needs to be set, but will be
    // overridden by SERIES_INFO_INACCESSIBLE.
    //
    REBARR *varlist = Alloc_Singular(SERIES_FLAG_STACK | NODE_FLAG_MANAGED);
    SET_SER_FLAGS(varlist, SERIES_MASK_CONTEXT);
    SET_SER_INFO(varlist, SERIES_INFO_INACCESSIBLE);
    MISC(varlist).meta = nullptr;

    RELVAL *rootvar = RESET_CELL(ARR_SINGLE(varlist), REB_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = a;
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

        // The ACTION_FLAG_LEAVE/ACTION_FLAG_RETURN tricks for definitional
        // scoping make it seem like a generator authored more code in the
        // action's body...but the code isn't *actually* there and an
        // optimized internal trick is used.  Fake the code if needed.

        REBVAL *example;
        REBCNT real_body_index;
        if (ACT_DISPATCHER(a) == &Voider_Dispatcher) {
            example = Get_System(SYS_STANDARD, STD_PROC_BODY);
            real_body_index = 4;
        }
        else if (GET_ACT_FLAG(a, ACTION_FLAG_RETURN)) {
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

            RESET_VAL_HEADER_EXTRA(slot, REB_GROUP, 0); // clear VAL_FLAG_LINE
            INIT_VAL_ARRAY(slot, VAL_ARRAY(body));
            VAL_INDEX(slot) = 0;
            INIT_BINDING(slot, a); // relative binding
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
    const REBVAL *code,
    REBFLGS mkf_flags // MKF_RETURN, etc.
) {
    assert(IS_BLOCK(spec) and IS_BLOCK(code));

    REBACT *a = Make_Action(
        Make_Paramlist_Managed_May_Fail(spec, mkf_flags),
        &Null_Dispatcher, // will be overwritten if non-[] body
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        1 // details array capacity
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
        else if (GET_VAL_FLAG(value, ACTION_FLAG_VOIDER)) {
            ACT_DISPATCHER(a) = &Voider_Dispatcher;
        }
        else if (GET_VAL_FLAG(value, ACTION_FLAG_RETURN)) {
            REBVAL *typeset = ACT_PARAM(a, ACT_NUM_PARAMS(a));
            assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);
            if (not TYPE_CHECK(typeset, REB_MAX_NULLED)) // what do [] returns
                ACT_DISPATCHER(a) = &Returner_Dispatcher; // error when run
        }
        else {
            // Keep the Null_Dispatcher passed in above
        }

        // Reusing EMPTY_ARRAY won't allow adding ARRAY_FLAG_FILE_LINE bits
        //
        copy = Make_Arr_Core(1, NODE_FLAG_MANAGED);
    }
    else { // body not empty, pick dispatcher based on output disposition

        if (GET_VAL_FLAG(value, ACTION_FLAG_INVISIBLE))
            ACT_DISPATCHER(a) = &Elider_Dispatcher; // no f->out mutation
        else if (GET_VAL_FLAG(value, ACTION_FLAG_VOIDER))
            ACT_DISPATCHER(a) = &Voider_Dispatcher; // forces f->out void
        else if (GET_VAL_FLAG(value, ACTION_FLAG_RETURN))
            ACT_DISPATCHER(a) = &Returner_Dispatcher; // type checks f->out
        else
            ACT_DISPATCHER(a) = &Unchecked_Dispatcher; // unchecked f->out

        copy = Copy_And_Bind_Relative_Deep_Managed(
            code, // new copy has locals bound relatively to the new action
            ACT_PARAMLIST(a),
            TS_WORD
        );
    }

    RELVAL *body = RESET_CELL(ARR_HEAD(ACT_DETAILS(a)), REB_BLOCK);
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
    if (FS_TOP->flags.bits & DO_FLAG_CONST)
        SET_VAL_FLAG(body, VALUE_FLAG_CONST); // Inherit_Const() needs REBVAL*

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
    GENERIC_HOOK hook = Generic_Hooks[kind];
    return hook(f, verb); // note: QUOTED! will re-dispatch to Generic_Hooks
}


//
//  Null_Dispatcher: C
//
// If you write `func [...] []` it uses this dispatcher instead of running
// Eval_Core_Throws() on an empty block.  This serves more of a point than
// it sounds, because you can make fast stub actions that only cost if they
// are HIJACK'd (e.g. ASSERT is done this way).
//
REB_R Null_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE_OR_DUMMY(f));
    assert(VAL_LEN_AT(ARR_HEAD(details)) == 0);
    UNUSED(details);

    return nullptr;
}


//
//  Void_Dispatcher: C
//
// Analogue to Null_Dispatcher() for `func [return: <void> ...] []`.
//
REB_R Void_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(VAL_LEN_AT(ARR_HEAD(details)) == 0);
    UNUSED(details);

    return Init_Void(f->out);
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


// Common behavior shared by dispatchers which execute on blocks of code.
//
inline static bool Interpreted_Dispatch_Throws(REBVAL *out, REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    RELVAL *body = ARR_HEAD(details);
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    // Whether the action body executes mutably is independent of the parent
    // frame's mutability disposition (possible exception: const functions?)
    // It depends on the mutability captured at the time of the action's
    // creation.  This enables things like calling a module based on Rebol2
    // expectations from modern Ren-C code.
    //
    bool mutability = NOT_VAL_FLAG(body, VALUE_FLAG_CONST);
    return Do_At_Mutability_Throws(
        out, // Note that elider_Dispatcher() does not overwrite f->out
        VAL_ARRAY(body),
        0,
        SPC(f->varlist),
        mutability
    );
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
    if (Interpreted_Dispatch_Throws(f->out, f)) // action body is a BLOCK!
        return R_THROWN;
    return Init_Void(f->out);
}


//
//  Returner_Dispatcher: C
//
// Runs block, ensure type matches RETURN: [...] specification, else fail.
//
// Note: Natives get this check only in the debug build, but not here (their
// dispatcher *is* the native!)  So the extra check is in Eval_Core_Throws().
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
    // !!! It would be nice to use the frame's spare "cell" for the thrownaway
    // result, but Fetch_Next code expects to use the cell.
    //
    DECLARE_LOCAL (dummy);
    SET_END(dummy);

    if (Interpreted_Dispatch_Throws(dummy, f)) {
        Move_Value(f->out, dummy);
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
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    RELVAL *hijacker = ARR_HEAD(details);

    // We need to build a new frame compatible with the hijacker, and
    // transform the parameters we've gathered to be compatible with it.
    //
    if (Redo_Action_Throws(f, VAL_ACTION(hijacker)))
        return R_THROWN;

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
    //
    // We can't do the prelude into f->out in the case that this is an
    // adaptation of an invisible (e.g. DUMP).  Would be nice to use the frame
    // spare cell but can't as Fetch_Next() uses it.

    DECLARE_LOCAL (dummy);
    bool mutability = NOT_VAL_FLAG(prelude, VALUE_FLAG_CONST);
    if (Do_At_Mutability_Throws(
        dummy,
        VAL_ARRAY(prelude),
        VAL_INDEX(prelude),
        SPC(f->varlist),
        mutability
    )){
        Move_Value(f->out, dummy);
        return R_THROWN;
    }

    FRM_PHASE(f) = VAL_ACTION(adaptee);
    FRM_BINDING(f) = VAL_BINDING(adaptee);

    return R_REDO_CHECKED; // the redo will use the updated phase/binding
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

    REBVAL *inner = KNOWN(ARR_AT(details, 0)); // same args as f
    assert(IS_ACTION(inner));
    REBVAL *outer = KNOWN(ARR_AT(details, 1)); // takes 1 arg (a FRAME!)
    assert(IS_ACTION(outer));

    assert(GET_SER_FLAG(f->varlist, SERIES_FLAG_STACK));

    // We want to call OUTER with a FRAME! value that will dispatch to INNER
    // when (and if) it runs DO on it.  That frame is the one built for this
    // call to the encloser.  If it isn't managed, there's no worries about
    // user handles on it...so just take it.  Otherwise, "steal" its vars.
    //
    REBCTX *c = Steal_Context_Vars(CTX(f->varlist), NOD(FRM_PHASE(f)));
    LINK(c).keysource = NOD(VAL_ACTION(inner));
    CLEAR_SER_FLAG(c, SERIES_FLAG_STACK);

    assert(GET_SER_INFO(f->varlist, SERIES_INFO_INACCESSIBLE)); // look dead

    // f->varlist may or may not have wound up being managed.  It was not
    // allocated through the usual mechanisms, so if unmanaged it's not in
    // the tracking list Init_Any_Context() expects.  Just fiddle the bit.
    //
    SET_SER_FLAG(c, NODE_FLAG_MANAGED);

    // When the DO of the FRAME! executes, we don't want it to run the
    // encloser again (infinite loop).
    //
    REBVAL *rootvar = CTX_ARCHETYPE(c);
    rootvar->payload.any_context.phase = VAL_ACTION(inner);
    INIT_BINDING_MAY_MANAGE(rootvar, VAL_BINDING(inner));

    Move_Value(FRM_CELL(f), rootvar); // user may DO this, or not...

    // We don't actually know how long the frame we give back is going to
    // live, or who it might be given to.  And it may contain things like
    // bindings in a RETURN or a VARARGS! which are to the old varlist, which
    // may not be managed...and so when it goes off the stack it might try
    // and think that since nothing managed it then it can be freed.  Go
    // ahead and mark it managed--even though it's dead--so that returning
    // won't free it if there are outstanding references.
    //
    // Note that since varlists aren't added to the manual series list, the
    // bit must be tweaked vs. using ENSURE_ARRAY_MANAGED.
    //
    SET_SER_FLAG(f->varlist, NODE_FLAG_MANAGED);

    const bool fully = true;
    if (Apply_Only_Throws(f->out, fully, outer, FRM_CELL(f), rebEND))
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
    FRM_PHASE(f) = VAL_ACTION(chained);
    FRM_BINDING(f) = VAL_BINDING(chained);

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
bool Get_If_Word_Or_Path_Throws(
    REBVAL *out,
    REBSTR **opt_name_out,
    const RELVAL *v,
    REBSPC *specifier,
    bool push_refinements
) {
    if (IS_WORD(v)) {
        *opt_name_out = VAL_WORD_SPELLING(v);
        Move_Opt_Var_May_Fail(out, v, specifier);
    }
    else if (IS_PATH(v)) {
        REBSPC *derived = Derive_Specifier(specifier, v);
        if (Eval_Path_Throws_Core(
            out,
            opt_name_out, // requesting says we run functions (not GET-PATH!)
            VAL_ARRAY(v),
            VAL_INDEX(v),
            derived,
            NULL, // `setval`: null means don't treat as SET-PATH!
            DO_MASK_DEFAULT | (push_refinements
                ? DO_FLAG_PUSH_PATH_REFINEMENTS // pushed in reverse order
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

