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
    REBARR *arr;
    REBLEN num_visible;
    RELVAL *dest;
    bool just_words;
};

// Reconstitute parameter back into a full value, e.g. REB_P_REFINEMENT
// becomes `/spelling`.
//
// !!! See notes on Is_Param_Hidden() for why caller isn't filtering locals.
//
static bool Params_Of_Hook(
    REBVAL *param,
    REBFLGS flags,
    void *opaque
){
    struct Params_Of_State *s = cast(struct Params_Of_State*, opaque);

    if (not (flags & PHF_SORTED_PASS)) {
        ++s->num_visible;  // first pass we just count unspecialized params
        return true;
    }

    if (not s->arr) {  // if first step on second pass, make the array
        s->arr = Make_Array(s->num_visible);
        s->dest = STABLE(ARR_HEAD(s->arr));
    }

    Init_Any_Word(s->dest, REB_WORD, VAL_PARAM_SPELLING(param));

    if (not s->just_words) {
        if (
            not (flags & PHF_UNREFINED)
            and TYPE_CHECK(param, REB_TS_REFINEMENT)
        ){
            Refinify(SPECIFIC(s->dest));
        }

        switch (VAL_PARAM_CLASS(param)) {
          case REB_P_NORMAL:
          case REB_P_OUTPUT:
            break;

          case REB_P_HARD_QUOTE:
            Quotify(SPECIFIC(s->dest), 1);
            break;

          case REB_P_MODAL:
            if (flags & PHF_DEMODALIZED) {
                // associated refinement specialized out
            }
            else
                Symify(SPECIFIC(s->dest));
            break;

          case REB_P_SOFT_QUOTE:
            Getify(SPECIFIC(s->dest));
            break;

          default:
            assert(false);
            DEAD_END;
        }
    }

    ++s->dest;
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
    s.arr = nullptr;
    s.num_visible = 0;
    s.just_words = just_words;

    For_Each_Unspecialized_Param(act, &Params_Of_Hook, &s);

    if (not s.arr)
        return Make_Array(1); // no unspecialized parameters, empty array

    TERM_ARRAY_LEN(s.arr, s.num_visible);
    ASSERT_ARRAY(s.arr);
    return s.arr;
}


static bool Typesets_Of_Hook(
    REBVAL *param,
    REBFLGS flags,
    void *opaque
){
    struct Params_Of_State *s = cast(struct Params_Of_State*, opaque);

    if (not (flags & PHF_SORTED_PASS)) {
        ++s->num_visible;  // first pass we just count unspecialized params
        return true;
    }

    if (not s->arr) {  // if first step on second pass, make the array
        s->arr = Make_Array(s->num_visible);
        s->dest = STABLE(ARR_HEAD(s->arr));
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
    s.just_words = false;  // (ignored)

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
    REBDSP dsp_orig,
    REBDSP *definitional_return_dsp
){
    assert(IS_BLOCK(spec));

    enum Reb_Spec_Mode mode = SPEC_MODE_NORMAL;

    bool refinement_seen = false;

    unstable const RELVAL* value = VAL_ARRAY_AT(spec);

    while (NOT_END(value)) {
        const RELVAL* item = STABLE_HACK(value);  // "faked"
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

            if (IS_PARAM(DS_TOP))
                Move_Value(DS_PUSH(), EMPTY_BLOCK);  // need block in position

            if (IS_BLOCK(DS_TOP)) {  // in right spot to push notes/title
                Init_Text(DS_PUSH(), Copy_String_At(item));
            }
            else {  // !!! A string was already pushed.  Should we append?
                assert(IS_TEXT(DS_TOP));
                Init_Text(DS_TOP, Copy_String_At(item));
            }

            if (DS_TOP == DS_AT(dsp_orig + 3))
                *flags |= MKF_HAS_DESCRIPTION;
            else
                *flags |= MKF_HAS_NOTES;

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
            else if (0 == CT_String(item, Root_Void_Tag, strict)) {
                *flags |= MKF_IS_VOIDER;  // use Voider_Dispatcher()

                // Fake as if they said [void!] !!! make more efficient
                //
                item = Get_System(SYS_STANDARD, STD_PROC_RETURN_TYPE);
                goto process_typeset_block;
            }
            else if (0 == CT_String(item, Root_Elide_Tag, strict)) {
                *flags |= MKF_IS_ELIDER;

                // Fake as if they said [<invisible>] !!! make more efficient
                //
                item = Get_System(SYS_STANDARD, STD_ELIDER_RETURN_TYPE);
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
            unstable REBVAL* param;
            if (IS_PARAM(DS_TOP)) {
                REBSPC* derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
                Init_Block(
                    DS_PUSH(),
                    Copy_Array_At_Deep_Managed(
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        derived
                    )
                );

                param = DS_TOP - 1;  // volatile if you DS_PUSH()!
            }
            else {
                assert(IS_TEXT(DS_TOP));  // !!! are blocks after notes good?

                if (IS_VOID_RAW(DS_TOP - 2)) {
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

                REBSPC* derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
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
            REBSPC* derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
            VAL_TYPESET_LOW_BITS(param) = 0;
            VAL_TYPESET_HIGH_BITS(param) = 0;
            Add_Typeset_Bits_Core(
                param,
                ARR_HEAD(VAL_ARRAY(item)),
                derived
            );
            if (was_refinement)
                TYPE_SET(param, REB_TS_REFINEMENT);

            *flags |= MKF_HAS_TYPES;
            continue;
        }

    //=//// ANY-WORD! PARAMETERS THEMSELVES (MAKE TYPESETS w/SYMBOL) //////=//

        bool quoted = false;  // single quoting level used as signal in spec
        if (VAL_NUM_QUOTES(item) > 0) {
            if (VAL_NUM_QUOTES(item) > 1)
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
            quoted = true;
        }

        REBCEL(const*) cell = VAL_UNESCAPED(item);
        enum Reb_Kind kind = CELL_KIND(cell);

        const REBSTR* spelling = nullptr;  // avoids compiler warning
        Reb_Param_Class pclass = REB_P_DETECT;  // error if not changed below

        bool refinement = false;  // paths with blanks at head are refinements
        if (ANY_PATH_KIND(kind)) {
            if (not IS_REFINEMENT_CELL(cell))
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

            refinement = true;
            refinement_seen = true;

            // !!! If you say [<with> x /foo y] the <with> terminates and a
            // refinement is started.  Same w/<local>.  Is this a good idea?
            // Note that historically, help hides any refinements that appear
            // behind a /local, but this feature has no parallel in Ren-C.
            //
            mode = SPEC_MODE_NORMAL;

            spelling = VAL_REFINEMENT_SPELLING(cell);
            if (STR_SYMBOL(spelling) == SYM_LOCAL)  // /LOCAL
                if (ANY_WORD_KIND(KIND3Q_BYTE(item + 1)))  // END is 0
                    fail (Error_Legacy_Local_Raw(spec));  // -> <local>

            if (CELL_KIND(cell) == REB_GET_PATH) {
                if (not quoted)
                    pclass = REB_P_SOFT_QUOTE;
            }
            else if (CELL_KIND(cell) == REB_PATH) {
                if (quoted)
                    pclass = REB_P_HARD_QUOTE;
                else
                    pclass = REB_P_NORMAL;
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
                pclass = REB_P_LOCAL;
                spelling = VAL_PREDICATE1_SPELLING(cell);
            }
        }
        else if (ANY_WORD_KIND(kind)) {
            spelling = VAL_WORD_SPELLING(cell);

            if (kind == REB_SET_WORD) {
                if (VAL_WORD_SYM(cell) == SYM_RETURN and not quoted) {
                    pclass = REB_P_LOCAL;
                }
                else if (not quoted) {
                    refinement = true;  // sets REB_TS_REFINEMENT (optional)
                    pclass = REB_P_OUTPUT;
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
                    if (not quoted)
                        pclass = REB_P_SOFT_QUOTE;
                }
                else if (kind == REB_WORD) {
                    if (quoted)
                        pclass = REB_P_HARD_QUOTE;
                    else
                        pclass = REB_P_NORMAL;
                }
                else if (kind == REB_SYM_WORD) {
                    if (not quoted)
                        pclass = REB_P_MODAL;
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

        const REBSTR* canon = STR_CANON(spelling);
        if (STR_SYMBOL(canon) == SYM_RETURN and pclass != REB_P_LOCAL) {
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
        // If the typeset bits contain REB_NULL, that indicates <opt>.
        // But Is_Param_Endable() indicates <end>.

        if (pclass == REB_P_LOCAL) {
            Init_Param(
                DS_PUSH(),
                REB_P_LOCAL,
                spelling,  // don't canonize, see #2258
                TS_OPT_VALUE
            );
        }
        else if (refinement) {
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
        if (STR_SYMBOL(canon) == SYM_RETURN) {
            if (*definitional_return_dsp != 0) {
                DECLARE_LOCAL(word);
                Init_Word(word, canon);
                fail (Error_Dup_Vars_Raw(word));  // most dup checks are later
            }
            if (pclass == REB_P_LOCAL)
                *definitional_return_dsp = DSP;  // RETURN: explicit, tolerate
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
    // separately do canon comparison of their symbols to find them.
    //
    // Note: Since RETURN's typeset holds types that need to be checked at
    // the end of the function run, it is moved to a predictable location:
    // first slot of the paramlist.  Initially it was the last slot...but this
    // enables adding more arguments/refinements/locals in derived functions.

    if (flags & MKF_RETURN) {
        if (definitional_return_dsp == 0) { // no explicit RETURN: pure local
            //
            // While default arguments disallow ACTION!, VOID!, and NULL...
            // they are allowed to return anything.  Generally speaking, the
            // checks are on the input side, not the output.
            //
            Init_Param(
                DS_PUSH(),
                REB_P_LOCAL,
                Canon(SYM_RETURN),
                TS_OPT_VALUE
                    | FLAGIT_KIND(REB_TS_INVISIBLE)  // return @() intentional
            );
            definitional_return_dsp = DSP;

            Move_Value(DS_PUSH(), EMPTY_BLOCK);
            Move_Value(DS_PUSH(), EMPTY_TEXT);
        }
        else {
            unstable REBVAL *param = DS_AT(definitional_return_dsp);
            assert(
                VAL_PARAM_CLASS(param) == REB_P_LOCAL
                or VAL_PARAM_CLASS(param) == REB_P_SEALED  // !!! review reuse
            );
            assert(HEART_BYTE(param) == REB_TYPESET);
            UNUSED(param);
        }

        // definitional_return handled specially when paramlist copied
        // off of the stack...moved to head position.

        flags |= MKF_HAS_RETURN;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    REBLEN num_slots = (DSP - dsp_orig) / 3;

    // There should be no more pushes past this point, so a stable pointer
    // into the stack for the definitional return can be found.
    //
    unstable REBVAL *definitional_return =
        definitional_return_dsp == 0
            ? nullptr
            : DS_AT(definitional_return_dsp);

    // Must make the function "paramlist" even if "empty", for identity.
    //
    // !!! In order to facilitate adding to the frame in the copy and
    // relativize step to add LET variables, don't pass SERIES_FLAG_FIXED_SIZE
    // in the creation step.  This formats cells in such a way that the
    // series mechanically cannot be expanded even if the flag is removed.
    // Instead, add it onto a series allocated as resizable.  This is likely
    // temporary--as LET mechanics should use some form of "virtual binding".
    //
    REBARR *paramlist = Make_Array_Core(
        num_slots,
        SERIES_MASK_PARAMLIST & ~(SERIES_FLAG_FIXED_SIZE)
    );
    SET_SERIES_FLAG(paramlist, FIXED_SIZE);

    if (flags & MKF_HAS_RETURN)
        SER(paramlist)->header.bits |= PARAMLIST_FLAG_HAS_RETURN;

  blockscope {
    REBVAL *dest = Voidify_Rootparam(paramlist) + 1;

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but note that a fail () cannot happen while binders are
    // in effect UNLESS the BUF_COLLECT contains information to undo it!
    // There's no BUF_COLLECT here, so don't fail while binder in effect.
    //
    // (This is why we wait until the parameter list gathering process
    // is over to do the duplicate checks--it can fail.)
    //
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    const REBSTR *duplicate = nullptr;

    unstable REBVAL *src = DS_AT(dsp_orig + 1) + 3;

    if (definitional_return) {
        assert(flags & MKF_RETURN);
        Move_Value(dest, definitional_return);
        ++dest;
    }

    // Weird due to Spectre/MSVC: https://stackoverflow.com/q/50399940
    //
    for (; src != DS_TOP + 1; src += 3) {
        if (not Is_Param_Sealed(src)) {  // allow reuse of sealed names
            if (not Try_Add_Binder_Index(&binder, VAL_PARAM_CANON(src), 1020))
                duplicate = VAL_PARAM_SPELLING(src);
        }

        if (definitional_return and src == definitional_return)
            continue;

        Move_Value(dest, src);
        ++dest;
    }

    // Must remove binder indexes for all words, even if about to fail
    //
    src = DS_AT(dsp_orig + 1) + 3;

    // Weird due to Spectre/MSVC: https://stackoverflow.com/q/50399940
    //
    for (; src != DS_TOP + 1; src += 3, ++dest) {
        if (Is_Param_Sealed(src))
            continue;
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

    if (flags & (MKF_HAS_DESCRIPTION | MKF_HAS_TYPES | MKF_HAS_NOTES))
        *meta = Copy_Context_Shallow_Managed(VAL_CONTEXT(Root_Action_Meta));
    else
        *meta = nullptr;

    // If a description string was gathered, it's sitting in the first string
    // slot, the third cell we pushed onto the stack.  Extract it if so.
    //
    if (flags & MKF_HAS_DESCRIPTION) {
        assert(IS_TEXT(DS_AT(dsp_orig + 3)));
        Move_Value(
            CTX_VAR(*meta, STD_ACTION_META_DESCRIPTION),
            DS_AT(dsp_orig + 3)
        );
    }

    // Only make `parameter-types` if there were blocks in the spec
    //
    if (flags & MKF_HAS_TYPES) {
        REBARR *types_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_VARLIST | NODE_FLAG_MANAGED
        );
        MISC_META_NODE(types_varlist) = nullptr;  // GC sees, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(types_varlist), paramlist);

        REBVAL *rootvar = RESET_CELL(
            STABLE(ARR_HEAD(types_varlist)),
            REB_OBJECT,
            CELL_MASK_CONTEXT
        );
        INIT_VAL_CONTEXT_VARLIST(rootvar, types_varlist);  // "canon FRAME!"
        INIT_VAL_CONTEXT_PHASE(rootvar, nullptr);
        INIT_BINDING(rootvar, UNBOUND);

        REBVAL *dest = rootvar + 1;
        const RELVAL *param = ARR_AT(paramlist, 1);

        unstable REBVAL *src = DS_AT(dsp_orig + 2);
        src += 3;

        if (definitional_return) {
            //
            // We put the return note in the top-level meta information, not
            // on the local itself (the "return-ness" is a distinct property
            // of the function from what word is used for RETURN:, and it
            // is possible to use the word RETURN for a local or refinement
            // argument while having nothing to do with the exit value of
            // the function.)
            //
            if (NOT_END(VAL_ARRAY_AT(definitional_return + 1))) {
                Move_Value(
                    CTX_VAR(*meta, STD_ACTION_META_RETURN_TYPE),
                    &definitional_return[1]
                );
            }

            Init_Nulled(dest); // clear the local RETURN: var's description
            SET_CELL_FLAG(dest, ARG_MARKED_CHECKED);
            ++dest;
            ++param;
        }

        for (; src <= DS_TOP; src += 3) {
            assert(IS_BLOCK(src));
            if (definitional_return and src == definitional_return + 1)
                continue;

            if (IS_END(VAL_ARRAY_AT(src)))
                Init_Nulled(dest);
            else
                Move_Value(dest, src);

            if (Is_Param_Hidden(param, param))  // special = param
                SET_CELL_FLAG(dest, ARG_MARKED_CHECKED);
            ++dest;
            ++param;
        }
        assert(IS_END(param));

        TERM_ARRAY_LEN(types_varlist, num_slots);

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
        MISC_META_NODE(notes_varlist) = nullptr;  // GC sees, must initialize
        INIT_CTX_KEYLIST_SHARED(CTX(notes_varlist), paramlist);

        REBVAL *rootvar = RESET_CELL(
            ARR_HEAD(notes_varlist),
            REB_OBJECT,
            CELL_MASK_CONTEXT
        );
        INIT_VAL_CONTEXT_VARLIST(rootvar, notes_varlist); // canon FRAME!
        INIT_VAL_CONTEXT_PHASE(rootvar, nullptr);
        INIT_BINDING(rootvar, UNBOUND);

        const RELVAL *param = ARR_AT(paramlist, 1);
        REBVAL *dest = rootvar + 1;

        unstable REBVAL *src = DS_AT(dsp_orig + 3);
        src += 3;

        if (definitional_return) {
            //
            // See remarks on the return type--the RETURN is documented in
            // the top-level META-OF, not the "incidentally" named RETURN
            // parameter in the list
            //
            if (VAL_LEN_HEAD(definitional_return + 2) == 0)
                Init_Nulled(CTX_VAR(*meta, STD_ACTION_META_RETURN_NOTE));
            else {
                Move_Value(
                    CTX_VAR(*meta, STD_ACTION_META_RETURN_NOTE),
                    &definitional_return[2]
                );
            }

            Init_Nulled(dest);
            SET_CELL_FLAG(dest, ARG_MARKED_CHECKED);
            ++dest;
            ++param;
        }

        for (; src <= DS_TOP; src += 3) {
            assert(IS_TEXT(src));
            if (definitional_return and src == definitional_return + 2)
                continue;

            if (VAL_LEN_HEAD(src) == 0)
                Init_Nulled(dest);
            else
                Move_Value(dest, src);

            if (Is_Param_Hidden(param, param))  // param = special
                SET_CELL_FLAG(dest, ARG_MARKED_CHECKED);
            ++dest;
            ++param;
        }
        assert(IS_END(param));

        TERM_ARRAY_LEN(notes_varlist, num_slots);

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
// Note: While paramlists should ultimately carry SERIES_FLAG_FIXED_SIZE,
// the product of this routine might need to be added to.  And series that
// are created fixed size have special preparation such that they will trip
// more asserts.  So the fixed size flag is *not* added here, but ensured
// in the Make_Action() step.
//
REBARR *Make_Paramlist_Managed_May_Fail(
    REBCTX **meta,
    const REBVAL *spec,
    REBFLGS *flags  // flags may be modified to carry additional information
){
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
    Init_Unreadable_Void(DS_PUSH()); // paramlist[0] becomes ACT_ARCHETYPE()
    Move_Value(DS_PUSH(), EMPTY_BLOCK); // param_types[0] (object canon)
    Move_Value(DS_PUSH(), EMPTY_TEXT); // param_notes[0] (desc, then canon)

    // The process is broken up into phases so that the spec analysis code
    // can be reused in AUGMENT.
    //
    Push_Paramlist_Triads_May_Fail(
        spec,
        flags,
        dsp_orig,
        &definitional_return_dsp
    );
    REBARR *paramlist = Pop_Paramlist_With_Meta_May_Fail(
        meta,
        dsp_orig,
        *flags,
        definitional_return_dsp
    );

    // By default, all paramlists point to themselves as the termination of
    // the ancestor chain.
    //
    LINK_ANCESTOR_NODE(paramlist) = NOD(paramlist);
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
REBLEN Find_Param_Index(REBARR *paramlist, REBSTR *spelling)
{
    const REBSTR *canon = STR_CANON(spelling); // don't recalculate each time

    RELVAL *param = STABLE(ARR_AT(paramlist, 1));
    REBLEN len = ARR_LEN(paramlist);

    REBLEN n;
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
    REBCTX *meta,
    REBNAT dispatcher, // native C function called by Eval_Core
    option(REBCTX*) exemplar, // if provided, make consistent w/next level
    REBLEN details_capacity // capacity of ACT_DETAILS (including archetype)
){
    assert(details_capacity >= 1);  // must have room for archetype

    ASSERT_ARRAY_MANAGED(paramlist);  // paramlists/keylists, can be shared
    ASSERT_UNREADABLE_IF_DEBUG(ARR_HEAD(paramlist));  // unused at this time
    assert(NOT_ARRAY_FLAG(paramlist, HAS_FILE_LINE_UNMASKED));
    if (SER(paramlist)->header.bits & PARAMLIST_FLAG_HAS_RETURN) {
        RELVAL *param = STABLE(ARR_AT(paramlist, 1));
        assert(VAL_PARAM_SYM(param) == SYM_RETURN);
        UNUSED(param);
    }

    // "details" for an action is an array of cells which can be anything
    // the dispatcher understands it to be, by contract.  Terminate it
    // at the given length implicitly.
    //
    REBARR *details = Make_Array_Core(
        details_capacity,  // leave room for archetype
        SERIES_MASK_DETAILS | NODE_FLAG_MANAGED
    );
    RELVAL *archetype = STABLE(ARR_HEAD(details));
    RESET_CELL(archetype, REB_ACTION, CELL_MASK_ACTION);
    VAL_ACT_DETAILS_NODE(archetype) = NOD(details);

    EXTRA(Binding, archetype).node = UNBOUND;
    //
    // Leave rest of the cells in the capacity uninitialized (caller fills in)
    //
    TERM_ARRAY_LEN(details, details_capacity);

    LINK(details).dispatcher = dispatcher; // level of indirection, hijackable
    MISC_META_NODE(details) = NOD(meta);

    if (not exemplar) {
        //
        // No exemplar is used as a cue to set the "specialty" to paramlist,
        // so that Push_Action() can assign f->special directly from it in
        // dispatch, and be equal to f->param.
        //
        VAL_ACTION_SPECIALTY_OR_LABEL_NODE(archetype) = NOD(paramlist);
    }
    else {
        // The parameters of the paramlist should line up with the slots of
        // the exemplar (though some of these parameters may be hidden due to
        // specialization, see REB_TS_HIDDEN).
        //
        assert(GET_SERIES_FLAG(unwrap(exemplar), MANAGED));
        assert(CTX_LEN(unwrap(exemplar)) == ARR_LEN(paramlist) - 1);

        VAL_ACTION_SPECIALTY_OR_LABEL_NODE(archetype)
            = NOD(CTX_VARLIST(unwrap(exemplar)));
    }

    assert(NOT_ARRAY_FLAG(details, HAS_FILE_LINE_UNMASKED));

    REBACT *act = ACT(details); // now it's a legitimate REBACT

    // Precalculate cached function flags.  This involves finding the first
    // unspecialized argument which would be taken at a callsite, which can
    // be tricky to figure out with partial refinement specialization.  So
    // the work of doing that is factored into a routine (`PARAMETERS OF`
    // uses it as well).

    REBVAL *first_unspecialized = First_Unspecialized_Param(act);
    if (first_unspecialized) {
        switch (VAL_PARAM_CLASS(first_unspecialized)) {
          case REB_P_NORMAL:
          case REB_P_OUTPUT:
            break;

          case REB_P_HARD_QUOTE:
          case REB_P_MODAL:
          case REB_P_SOFT_QUOTE:
            SET_ACTION_FLAG(act, QUOTES_FIRST);
            break;

          default:
            assert(false);
        }

        if (TYPE_CHECK(first_unspecialized, REB_TS_SKIPPABLE))
            SET_ACTION_FLAG(act, SKIPPABLE_FIRST);
    }

    assert(ACT_PARAMLIST(act) == paramlist);  // via specialty
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
    REBARR *varlist = Alloc_Singular(NODE_FLAG_MANAGED);
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
        a = VAL_ACTION(ARR_AT(ACT_DETAILS(a), 1));
        // !!! Review what should happen to binding
    }

    REBARR *details = ACT_DETAILS(a);

    // !!! Should the binding make a difference in the returned body?  It is
    // exposed programmatically via CONTEXT OF.
    //
    UNUSED(binding);

    if (
        ACT_DISPATCHER(a) == &Void_Dispatcher
        or ACT_DISPATCHER(a) == &Empty_Dispatcher
        or ACT_DISPATCHER(a) == &Unchecked_Dispatcher
        or ACT_DISPATCHER(a) == &Voider_Dispatcher
        or ACT_DISPATCHER(a) == &Returner_Dispatcher
        or ACT_DISPATCHER(a) == &Block_Dispatcher
    ){
        // Interpreted code, the body is a block with some bindings relative
        // to the action.

        unstable RELVAL *body = ARR_AT(details, IDX_DETAILS_1);

        // The PARAMLIST_HAS_RETURN tricks for definitional return make it
        // seem like a generator authored more code in the action's body...but
        // the code isn't *actually* there and an optimized internal trick is
        // used.  Fake the code if needed.

        REBVAL *example;
        REBLEN real_body_index;
        if (ACT_DISPATCHER(a) == &Voider_Dispatcher) {
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

            RELVAL *slot = STABLE(ARR_AT(fake, real_body_index));  // #BODY
            assert(IS_ISSUE(slot));

            // Note: clears VAL_FLAG_LINE
            //
            RESET_VAL_HEADER(slot, REB_GROUP, CELL_FLAG_FIRST_IS_NODE);
            INIT_VAL_NODE(slot, VAL_ARRAY(body));
            VAL_INDEX_RAW(slot) = 0;
            INIT_BINDING(slot, a);  // relative binding

            maybe_fake_body = fake;
        }

        // Cannot give user a relative value back, so make the relative
        // body specific to a fabricated expired frame.  See #2221

        RESET_VAL_HEADER(out, REB_BLOCK, CELL_FLAG_FIRST_IS_NODE);
        INIT_VAL_NODE(out, maybe_fake_body);
        VAL_INDEX_RAW(out) = 0;
        INIT_BINDING(out, Make_Expired_Frame_Ctx_Managed(a));
        return;
    }

    if (ACT_DISPATCHER(a) == &Specializer_Dispatcher) {
        //
        // The FRAME! stored in the body for the specialization has a phase
        // which is actually the function to be run.
        //
        const REBVAL *frame = CTX_ARCHETYPE(ACT_EXEMPLAR(a));
        assert(IS_FRAME(frame));
        Move_Value(out, frame);
        return;
    }

    if (ACT_DISPATCHER(a) == &Generic_Dispatcher) {
        REBVAL *verb = DETAILS_AT(details, 1);
        assert(IS_WORD(verb));
        Move_Value(out, verb);
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
    if (IS_WORD(v) or IS_GET_WORD(v) or IS_SYM_WORD(v)) {
      get_as_word:
        Get_Word_May_Fail(out, v, specifier);
        if (IS_ACTION(out))
            INIT_ACTION_LABEL(out, VAL_WORD_SPELLING(v));
    }
    else if (
        IS_PATH(v) or IS_GET_PATH(v) or IS_SYM_PATH(v)
        or IS_TUPLE(v) or IS_GET_TUPLE(v) or IS_SYM_TUPLE(v)
    ){
        if (ANY_WORD_KIND(HEART_BYTE(v)))  // e.g. `/`
            goto get_as_word;  // faster than calling Eval_Path_Throws_Core?

        if (Eval_Path_Throws_Core(
            out,
            v,  // !!! may not be array based
            specifier,
            NULL,  // `setval`: null means don't treat as SET-PATH!
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
