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
// Copyright 2012-2016 Rebol Open Source Contributors
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
/*
    Structure of functions:

        spec - interface spec block
        body - body code
        args - args list (see below)

    Args list is a block of word+values:

        word - word, 'word, :word, /word
        value - typeset! or blank (valid datatypes)

    Args list provides:

        1. specifies arg order, arg kind (e.g. 'word)
        2. specifies valid datatypes (typesets)
        3. used for word and type in error output
        4. used for debugging tools (stack dumps)
        5. not used for MOLD (spec is used)
        6. used as a (pseudo) frame of function variables

*/

#include "sys-core.h"

//
//  List_Func_Words: C
// 
// Return a block of function words, unbound.
// Note: skips 0th entry.
//
REBARR *List_Func_Words(const REBVAL *func, REBOOL pure_locals)
{
    REBARR *array = Make_Array(VAL_FUNC_NUM_PARAMS(func));
    REBVAL *param = VAL_FUNC_PARAMS_HEAD(func);

    for (; !IS_END(param); param++) {
        enum Reb_Kind kind;

        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_NORMAL:
            kind = REB_WORD;
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

        Val_Init_Word(Alloc_Tail_Array(array), kind, VAL_PARAM_CASED(param));
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

    for (; !IS_END(typeset); typeset++) {
        REBVAL *value = Alloc_Tail_Array(array);

        assert(IS_TYPESET(typeset));
        *value = *typeset;

        // !!! It's already a typeset, but this will clear out the header
        // bits.  This may not be desirable over the long run (what if
        // a typeset wishes to encode hiddenness, protectedness, etc?)
        //
        VAL_RESET_HEADER(value, REB_TYPESET);
    }

    return array;
}


inline static void Swap_Values(RELVAL *value1, RELVAL *value2) {
    REBVAL temp = *KNOWN(value1);
    *value1 = *KNOWN(value2);
    *value2 = temp;
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
    if (flags & MKF_PUNCTUATES)
        header_bits |= FUNC_FLAG_PUNCTUATES;

    REBOOL durable = FALSE;

    // We want to be able to notice when words are duplicated, and a binder
    // can be used for that purpose.
    //
    struct Reb_Binder binder;
    INIT_BINDER(&binder);

    REBDSP dsp_orig = DSP;
    assert(DS_TOP == DS_AT(dsp_orig));

    RELVAL *definitional_return = NULL;
    RELVAL *definitional_leave = NULL;

    // As we go through the spec block, we push TYPESET! BLOCK! STRING! triples.
    // These will be split out into separate arrays after the process is done.
    // The first slot of the paramlist needs to be the function canon value,
    // while the other two first slots need to be rootkeys.  Get the process
    // started right after a BLOCK! so it's willing to take a string for
    // the function description--it will be extracted from the slot before
    // it is turned into a rootkey for param_notes.
    //
    DS_PUSH_TRASH; // paramlist[0] (will become FUNCTION! canon value)
    SET_TRASH_SAFE(DS_TOP);
    DS_PUSH(EMPTY_BLOCK); // param_types[0] (to be OBJECT! canon value, if any)
    DS_PUSH(EMPTY_STRING); // param_notes[0] (holds description, then canon)

    REBOOL has_description = FALSE;
    REBOOL has_types = FALSE;
    REBOOL has_notes = FALSE;

    // Trickier case: when the `func` or `proc` natives are used, they
    // must read the given spec the way a user-space generator might.
    // They must decide whether to add a specially handled RETURN
    // local, which will be given a tricky "native" definitional return
    //
    REBOOL convert_local = FALSE;

    REBOOL refinement_seen = FALSE;

    const RELVAL *item;
    for (item = VAL_ARRAY_AT(spec); NOT_END(item); item++) {

    //=//// STRING! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////=//

        if (IS_STRING(item)) {
            if (IS_TYPESET(DS_TOP))
                DS_PUSH(EMPTY_BLOCK); // need a block to be in position

            if (IS_BLOCK(DS_TOP)) { // we're in right spot to push notes/title
                DS_PUSH_TRASH;
                Val_Init_String(
                    DS_TOP,
                    Copy_String_Slimming(VAL_SERIES(item), VAL_INDEX(item), -1)
                );
            }
            else if (IS_STRING(DS_TOP)) {
                //
                // !!! A string was already pushed.  Should we append?
                //
                Val_Init_String(
                    DS_TOP,
                    Copy_String_Slimming(VAL_SERIES(item), VAL_INDEX(item), -1)
                );
            }
            else
                fail (Error(RE_MISC)); // should not be possible.

            if (DS_TOP == DS_AT(dsp_orig + 3))
                has_description = TRUE;
            else
                has_notes = TRUE;

            continue;
        }

    //=//// TAGS LIKE <local>, <no-return>, <punctuates>, etc. ////////////=//

        if (IS_TAG(item) && (flags & MKF_KEYWORDS)) {
            if (0 == Compare_String_Vals(item, ROOT_NO_RETURN_TAG, TRUE)) {
                flags &= ~MKF_RETURN;
            }
            else if (0 == Compare_String_Vals(item, ROOT_NO_LEAVE_TAG, TRUE)) {
                flags &= ~MKF_LEAVE;
            }
            else if (
                0 == Compare_String_Vals(item, ROOT_PUNCTUATES_TAG, TRUE)
            ) {
                header_bits |= FUNC_FLAG_PUNCTUATES;
            }
            else if (0 == Compare_String_Vals(item, ROOT_LOCAL_TAG, TRUE)) {
                convert_local = TRUE;
            }
            else if (0 == Compare_String_Vals(item, ROOT_DURABLE_TAG, TRUE)) {
                //
                // <durable> is currently a lesser version of what it
                // hopes to be, but signals what R3-Alpha called CLOSURE!
                // semantics.  Indicating that a typeset is durable in
                // the low-level will need to be done with some notation
                // that doesn't use "keywords"--perhaps a #[true] or a
                // #[false] picked up on by the typeset.
                //
                // !!! Enforce only at the head, if it's going to be
                // applying to everything??
                //
                durable = TRUE;
            }
            else
                fail (Error(RE_BAD_FUNC_DEF, item));

            continue;
        }

    //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) ////=//

        if (IS_BLOCK(item)) {
            if (IS_BLOCK(DS_TOP)) {
                //
                // Tried to give two blocks of types.  !!! Better error here
                //
                if (NOT(IS_BLANK(DS_TOP - 1))) // not [0] slot
                    fail (Error(RE_BAD_FUNC_DEF, item));

                // !!! Rebol2 had the ability to put a block in the first
                // slot before any parameters, in which you could put words.
                // This is deprecated in favor of the use of tags.  We permit
                // [catch] and [throw] during Rebol2 => Rebol3 migration,
                // but ignore them.
                //
                RELVAL *attribute = VAL_ARRAY_AT(item);
                for (; NOT_END(attribute); attribute++) {
                    if (IS_WORD(attribute)) {
                        if (VAL_WORD_SYM(attribute) == SYM_CATCH)
                            continue; // ignore it
                        if (VAL_WORD_SYM(attribute) == SYM_THROW)
                            continue; // ignore it
                    }
                    fail (Error(RE_BAD_FUNC_DEF, item));
                }

                continue;
            }

            // Save the block for parameter types.
            //
            REBVAL *typeset;
            if (IS_TYPESET(DS_TOP)) {
                typeset = DS_TOP;
                DS_PUSH_TRASH;
                Val_Init_Block(
                    DS_TOP,
                    Copy_Array_At_Deep_Managed(
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        IS_SPECIFIC(item)
                            ? VAL_SPECIFIER(const_KNOWN(item))
                            : VAL_SPECIFIER(spec)
                    )
                );
            }
            else if (IS_STRING(DS_TOP)) { // !!! are blocks after notes good?
                assert(IS_TYPESET(DS_TOP - 2));
                typeset = DS_TOP - 2;

                assert(IS_BLOCK(DS_TOP - 1));
                if (VAL_ARRAY(DS_TOP - 1) != EMPTY_ARRAY)
                    fail (Error(RE_BAD_FUNC_DEF, item));

                Val_Init_Block(
                    DS_TOP - 1,
                    Copy_Array_At_Deep_Managed(
                        VAL_ARRAY(item),
                        VAL_INDEX(item),
                        IS_SPECIFIC(item)
                            ? VAL_SPECIFIER(const_KNOWN(item))
                            : VAL_SPECIFIER(spec)
                    )
                );
            }
            else
                fail (Error(RE_MISC)); // shouldn't be possible

            // Turn block into typeset for parameter at current index.
            // Leaves VAL_TYPESET_SYM as-is.
            //
            Update_Typeset_Bits_Core(
                typeset,
                VAL_ARRAY_HEAD(item),
                IS_SPECIFIC(item)
                    ? VAL_SPECIFIER(const_KNOWN(item))
                    : VAL_SPECIFIER(spec),
                FALSE // `trap`: false means fail vs. return FALSE if error
            );

            // A hard quote can only get a void if it is an <end>.
            //
            if (VAL_PARAM_CLASS(typeset) == PARAM_CLASS_HARD_QUOTE) {
                if (TYPE_CHECK(typeset, REB_0)) {
                    REBVAL param_name;
                    Val_Init_Word(
                        &param_name, REB_WORD, VAL_PARAM_CASED(typeset)
                    );
                    fail (Error(RE_HARD_QUOTE_VOID, &param_name));
                }
            }

            has_types = TRUE;
            continue;
        }

    //=//// BAR! AS LOW-LEVEL MAKE FUNCTION! SIGNAL FOR <punctuates> //////=//

        if (IS_BAR(item)) { // !!! Review this notational choice
            header_bits |= FUNC_FLAG_PUNCTUATES;
            continue;
        }

    //=//// ANY-WORD! PARAMETERS THEMSELVES (MAKE TYPESETS w/SYMBOL) //////=//

        if (!ANY_WORD(item))
            fail (Error(RE_BAD_FUNC_DEF, item));

        // Make sure symbol not already in the parameter list, and then mark
        // in the hash table that it is present.  Any non-zero value is ok.
        //
        REBSTR *canon = VAL_WORD_CANON(item);
        if (NOT(Try_Add_Binder_Index(&binder, canon, 1020)))
            fail (Error(RE_DUP_VARS, item));

        // In rhythm of TYPESET! BLOCK! STRING! we want to be on a string spot
        // at the time of the push of each new typeset.
        //
        if (IS_TYPESET(DS_TOP))
            DS_PUSH(EMPTY_BLOCK);
        if (IS_BLOCK(DS_TOP))
            DS_PUSH(EMPTY_STRING);
        assert(IS_STRING(DS_TOP));

        // Allow "all datatypes but void".  Note that this is the <opt> sense
        // of void signal--not the <end> sense, which is controlled by a flag.
        // We do not canonize the saved symbol in the paramlist, see #2258.
        //
        DS_PUSH_TRASH;
        REBVAL *typeset = DS_TOP;
        Val_Init_Typeset(typeset, ~FLAGIT_KIND(REB_0), VAL_WORD_CASED(item));

        // All these would cancel a definitional return (leave has same idea):
        //
        //     func [return [integer!]]
        //     func [/value return]
        //     func [/local return] ;-- /local is not special in Ren-C
        //
        // ...although `return:` is explicitly tolerated ATM for compatibility
        // (despite violating the "pure locals are NULL" premise)

        if (STR_SYMBOL(canon) == SYM_RETURN) {
            assert(definitional_return == NULL);
            if (IS_SET_WORD(item))
                definitional_return = typeset; // RETURN: explicitly tolerated
            else
                flags &= ~MKF_RETURN;
        }
        else if (STR_SYMBOL(canon) == SYM_LEAVE) {
            assert(definitional_leave == NULL);
            if (IS_SET_WORD(item))
                definitional_leave = typeset; // LEAVE: is explicitly tolerated
            else
                flags &= ~MKF_LEAVE;
        }

        switch (VAL_TYPE(item)) {
        case REB_WORD:
            INIT_VAL_PARAM_CLASS(
                typeset,
                convert_local ? PARAM_CLASS_LOCAL : PARAM_CLASS_NORMAL
            );
            if (refinement_seen) VAL_TYPESET_BITS(typeset) |= FLAGIT_64(REB_0);
            break;

        case REB_GET_WORD:
            if (convert_local)
                fail (Error(RE_BAD_FUNC_DEF)); // what's a "quoted local"?
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_HARD_QUOTE);
            if (refinement_seen) VAL_TYPESET_BITS(typeset) |= FLAGIT_64(REB_0);
            break;

        case REB_LIT_WORD:
            if (convert_local)
                fail (Error(RE_BAD_FUNC_DEF)); // what's a "quoted local"?
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_SOFT_QUOTE);
            if (refinement_seen) VAL_TYPESET_BITS(typeset) |= FLAGIT_64(REB_0);
            break;

        case REB_REFINEMENT:
            refinement_seen = TRUE;
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_REFINEMENT);

            // !!! The typeset bits of a refinement are not currently used.
            // They are checked for TRUE or FALSE but this is done literally
            // by the code.  This means that every refinement has some spare
            // bits available in it for another purpose.

            // A refinement signals us to stop doing the locals conversion.
            // Historically, help hides any refinements that appear behind a
            // /local, so presumably it would do the same with <local>...
            // but this feature does not currently exist in Ren-C.
            //
            convert_local = FALSE;
            break;

        case REB_SET_WORD:
            // tolerate as-is if convert_local
            INIT_VAL_PARAM_CLASS(typeset, PARAM_CLASS_LOCAL);
            //
            // !!! Typeset bits of pure locals also not currently used,
            // though definitional return should be using it for the return
            // type of the function.
            //
            break;

        default:
            fail (Error(RE_BAD_FUNC_DEF, item));
        }
        assert(VAL_PARAM_CLASS(typeset) != PARAM_CLASS_0);

        // !!! This is a lame way of setting the durability, because it means
        // that there's no way a user with just `make function!` could do it.
        // However, it's a step closer to the solution and eliminating the
        // FUNCTION!/CLOSURE! distinction.
        //
        if (durable)
            SET_VAL_FLAG(typeset, TYPESET_FLAG_DURABLE);
    }

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
        if (definitional_leave == NULL) { // no LEAVE: pure local explicit
            REBSTR *canon_leave = Canon(SYM_LEAVE);
            Add_Binder_Index(&binder, canon_leave, 1020);

            DS_PUSH_TRASH;
            definitional_leave = DS_TOP;
            Val_Init_Typeset(DS_TOP, FLAGIT_64(REB_0), canon_leave);
            INIT_VAL_PARAM_CLASS(DS_TOP, PARAM_CLASS_LEAVE);
            DS_PUSH(EMPTY_BLOCK);
            DS_PUSH(EMPTY_STRING);
        }
        else {
            assert(VAL_PARAM_CLASS(definitional_leave) == PARAM_CLASS_LOCAL);
            INIT_VAL_PARAM_CLASS(definitional_leave, PARAM_CLASS_LEAVE);
        }
        header_bits |= FUNC_FLAG_LEAVE;
    }

    if (flags & MKF_RETURN) {
        if (definitional_return == NULL) { // no RETURN: pure local explicit
            REBSTR *canon_return = Canon(SYM_RETURN);
            Add_Binder_Index(&binder, canon_return, 1020);

            DS_PUSH_TRASH;
            definitional_return = DS_TOP;
            Val_Init_Typeset(DS_TOP, ALL_64, canon_return);
            INIT_VAL_PARAM_CLASS(definitional_return, PARAM_CLASS_RETURN);
            DS_PUSH(EMPTY_BLOCK);
            DS_PUSH(EMPTY_STRING);
            // no need to move it--it's already at the tail position
        }
        else {
            assert(VAL_PARAM_CLASS(definitional_return) == PARAM_CLASS_LOCAL);
            INIT_VAL_PARAM_CLASS(definitional_return, PARAM_CLASS_RETURN);

            Swap_Values(DS_TOP - 2, definitional_return);
            Swap_Values(DS_TOP - 1, definitional_return + 1);
            Swap_Values(DS_TOP, definitional_return + 2);
        }
        header_bits |= FUNC_FLAG_RETURN;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    REBCNT num_slots = (DSP - dsp_orig) / 3;

    // Must make the function "paramlist" even if "empty", for identity
    //
    REBARR *paramlist = Make_Array(num_slots);
    if (TRUE) {
        RELVAL *dest = ARR_HEAD(paramlist); // canon function value
        VAL_RESET_HEADER(dest, REB_FUNCTION);
        SET_VAL_FLAGS(dest, header_bits);
        dest->payload.function.paramlist = paramlist;
        dest->extra.binding = NULL;
        ++dest;

        REBVAL *src = DS_AT(dsp_orig + 1);
        src += 3;
        for (; src <= DS_TOP; src += 3, ++dest) {
            assert(IS_TYPESET(src));
            *dest = *src;
            Remove_Binder_Index(&binder, VAL_PARAM_CANON(dest));
        }
        SET_END(dest);

        SET_ARRAY_LEN(paramlist, num_slots);
        MANAGE_ARRAY(paramlist);

        // Make sure the parameter list does not expand.
        //
        // !!! Should more precautions be taken, at some point locking and
        // protecting the whole array?  (It will be changed more by the
        // caller, but after that.)
        //
        SET_ARR_FLAG(paramlist, SERIES_FLAG_FIXED_SIZE);
    }

    SHUTDOWN_BINDER(&binder);

    //=///////////////////////////////////////////////////////////////////=//
    //
    // BUILD META INFORMATION OBJECT (IF NEEDED)
    //
    //=///////////////////////////////////////////////////////////////////=//

    // !!! See notes on FUNCTION-META in %sysobj.r
    const REBCNT description = 1;
    const REBCNT parameter_types = 2;
    const REBCNT parameter_notes = 3;
    REBCTX *meta;

    if (has_description || has_types || has_notes) {
        meta = Copy_Context_Shallow(VAL_CONTEXT(ROOT_FUNCTION_META));
        MANAGE_ARRAY(CTX_VARLIST(meta));
        ARR_SERIES(paramlist)->link.meta = meta;
    }
    else
        ARR_SERIES(paramlist)->link.meta = NULL;

    // If a description string was gathered, it's sitting in the first string
    // slot, the third cell we pushed onto the stack.  Extract it if so.
    //
    if (has_description) {
        assert(IS_STRING(DS_AT(dsp_orig + 3)));
        *CTX_VAR(meta, 1) = *DS_AT(dsp_orig + 3);
    }

    // Only make `parameter-types` if there were blocks in the spec
    //
    if (has_types) {
        REBARR *types_varlist = Make_Array(num_slots);
        SET_ARR_FLAG(types_varlist, ARRAY_FLAG_CONTEXT_VARLIST);
        INIT_CTX_KEYLIST_SHARED(AS_CONTEXT(types_varlist), paramlist);

        RELVAL *dest = ARR_HEAD(types_varlist); // rootvar: canon FRAME! value
        VAL_RESET_HEADER(dest, REB_FRAME);
        dest->payload.any_context.varlist = types_varlist;
        dest->extra.binding = NULL;
        ++dest;

        REBVAL *src = DS_AT(dsp_orig + 2);
        src += 3;
        for (; src <= DS_TOP; src += 3, ++dest) {
            assert(IS_BLOCK(src));
            if (VAL_ARRAY_LEN_AT(src) == 0)
                SET_VOID(dest);
            else
                *dest = *src;
        }
        SET_END(dest);

        SET_ARRAY_LEN(types_varlist, num_slots);
        MANAGE_ARRAY(types_varlist);

        Val_Init_Context(
            CTX_VAR(meta, 2), REB_FRAME, AS_CONTEXT(types_varlist)
        );
    }

    // Only make `parameter-notes` if there were strings (besides description)
    //
    if (has_notes) {
        REBARR *notes_varlist = Make_Array(num_slots);
        SET_ARR_FLAG(notes_varlist, ARRAY_FLAG_CONTEXT_VARLIST);
        INIT_CTX_KEYLIST_SHARED(AS_CONTEXT(notes_varlist), paramlist);

        RELVAL *dest = ARR_HEAD(notes_varlist); // rootvar: canon FRAME! value
        VAL_RESET_HEADER(dest, REB_FRAME);
        dest->payload.any_context.varlist = notes_varlist;
        dest->extra.binding = NULL;
        ++dest;

        REBVAL *src = DS_AT(dsp_orig + 3);
        src += 3;
        for (; src <= DS_TOP; src += 3, ++dest) {
            assert(IS_STRING(src));
            if (SER_LEN(VAL_SERIES(src)) == 0)
                SET_VOID(dest);
            else
                *dest = *src;
        }
        SET_END(dest);

        SET_ARRAY_LEN(notes_varlist, num_slots);
        MANAGE_ARRAY(notes_varlist);

        Val_Init_Context(
            CTX_VAR(meta, 3), REB_FRAME, AS_CONTEXT(notes_varlist)
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
REBCNT Find_Param_Index(REBARR *paramlist, REBSTR *name)
{
    REBSTR *canon = STR_CANON(name); // don't recalculate each time

    RELVAL *param = ARR_AT(paramlist, 1);
    REBCNT len = ARR_LEN(paramlist);

    REBCNT n;
    for (n = 1; n < len; ++n, ++param) {
        if (
            name == VAL_PARAM_CASED(param)
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
//     REB_R Dispatcher(struct Reb_Frame *f) {...}
//
// The REBFUN returned is "archetypal" because individual REBVALs which hold
// the same REBFUN may differ in a per-REBVAL piece of "instance" data.
// (This is how one RETURN is distinguished from another--the instance
// data stored in the REBVAL identifies the pointer of the FRAME! to exit).
//
// Functions have an associated REBVAL-sized cell of data, accessible via
// FUNC_BODY().  This is where they can store information that will be
// available when the dispatcher is called.  Despite the name, it doesn't
// have to be an array--it can be any REBVAL.
//
REBFUN *Make_Function(
    REBARR *paramlist,
    REBNAT dispatcher, // native C function called by Do_Core
    REBFUN *opt_underlying // function which has size of actual frame to push
) {
    ASSERT_ARRAY_MANAGED(paramlist);

    RELVAL *rootparam = ARR_HEAD(paramlist);
    assert(IS_FUNCTION(rootparam)); // !!! body not fully formed...
    assert(rootparam->payload.function.paramlist == paramlist);
    assert(rootparam->extra.binding == NULL); // archetype

    // The "body" for a function can be any REBVAL.  It doesn't have to be
    // a block--it's anything that the dispatcher might wish to interpret.
    // It is allocated as a "singular" array--packed into sizeof(REBSER)
    // thanks to the END marker trick.

    REBARR *body_holder = Make_Singular_Array(BLANK_VALUE);
    MANAGE_ARRAY(body_holder);
    rootparam->payload.function.body_holder = body_holder;

    // The C function pointer is stored inside the REBSER node for the body.
    // Hence there's no need for a `switch` on a function class in Do_Core,
    // Having a level of indirection from the REBVAL bits themself also
    // facilitates the "Hijacker" to change multiple REBVALs behavior.

    ARR_SERIES(body_holder)->misc.dispatcher = dispatcher;

    // To avoid NULL checking when a function is called and looking for the
    // underlying function, put the functions own pointer in if needed
    //
    ARR_SERIES(paramlist)->misc.underlying
        = opt_underlying != NULL ? opt_underlying : AS_FUNC(paramlist);

    // Note: used to set the keys of natives as read-only so that the debugger
    // couldn't manipulate the values in a native frame out from under it,
    // potentially crashing C code (vs. just causing userspace code to
    // error).  That protection is now done to the frame series on reification
    // in order to be able to MAKE FRAME! and reuse the native's paramlist.

    return AS_FUNC(paramlist);
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
    REBARR *varlist = Make_Singular_Array(BLANK_VALUE);
    SET_ARR_FLAG(varlist, ARRAY_FLAG_CONTEXT_VARLIST);
    MANAGE_ARRAY(varlist);

    REBCTX *expired = AS_CONTEXT(varlist);
    SET_CTX_FLAG(expired, CONTEXT_FLAG_STACK); // don't set FLAG_ACCESSIBLE

    INIT_CTX_KEYLIST_SHARED(expired, FUNC_PARAMLIST(func));

    CTX_VALUE(expired)->payload.any_context.varlist = varlist;

    // Clients aren't supposed to ever be looking at the values for the
    // stackvars or the frame if it is expired.
    //
    ARR_SERIES(varlist)->misc.f = NULL;

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

    assert(IS_FUNCTION(func) && IS_FUNCTION_PLAIN(func));

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

        VAL_RESET_HEADER(slot, REB_GROUP);
        SET_VAL_FLAGS(slot, VALUE_FLAG_RELATIVE | VALUE_FLAG_LINE);
        INIT_VAL_ARRAY(slot, VAL_ARRAY(VAL_FUNC_BODY(func)));
        VAL_INDEX(slot) = 0;
        INIT_RELATIVE(slot, VAL_FUNC(func));
    }

    return fake_body;
}


//
//  Make_Plain_Function_May_Fail: C
// 
// This is the support routine behind `MAKE FUNCTION!`, FUNC, and PROC.
//
// Ren/C's schematic for the FUNC and PROC generators is *very* different
// from R3-Alpha, whose definition of FUNC was simply:
// 
//     make function! copy/deep reduce [spec body]
// 
// Not only does Ren/C's `make function!` already copy the spec and body,
// but FUNC and CLOS "use the internals to cheat".  They analyze and edit
// the spec, then potentially build an entity whose full "body" acts like:
// 
//     return: make function! [
//         [{Returns a value from a function.} value [<opt> any-value!]]
//         [exit/from/with (context-of 'return) :value]
//     ]
//     (body goes here)
// 
// This pattern addresses "Definitional Return" in a way that does not
// technically require building RETURN in as a language keyword in any
// specific form.  FUNC and CLOS optimize by not internally building
// or executing the equivalent body, but giving it back from BODY-OF.
// 
// NOTES:
// 
// The spec and body are copied--even for MAKE FUNCTION!--because:
// 
//    (a) It prevents tampering with the spec after it has been analyzed
//        by Make_Paramlist_Managed().  Such changes to the spec will not be
//        reflected in the actual behavior of the function.
// 
//    (b) The BLOCK! values inside the make-spec may actually be imaging
//        series at an index position besides the series head.  However,
//        the REBVAL for a FUNCTION! contains only three REBSER slots--
//        all in use, with no space for offsets.  A copy must be made
//        to truncate to the intended spec and body start (unless one
//        is willing to raise errors on non-head position series :-/)
// 
//    (c) Copying the root of the series into a series the user cannot
//        access makes it possible to "lie" about what the body "above"
//        is.  This gives FUNC and CLOS the edge to pretend to add
//        containing code and simulate its effects, while really only
//        holding onto the body the caller provided.  This trick may
//        prove useful for other optimizing generators.
// 
// While MAKE FUNCTION! has no RETURN, all functions still have EXIT as a
// non-definitional alternative.  Ren/C adds a /WITH refinement so it can
// behave equivalently to old-non-definitonal return.  There is even a way to
// identify specific points up the call stack to exit from via EXIT/FROM, so
// not having definitional return has several alternate options for generators
// that wish to use them.
// 
// This function will either successfully place a function value into
// `out` or not return...as a failed check on a function spec is
// raised as an error.
//
REBFUN *Make_Plain_Function_May_Fail(
    const REBVAL *spec,
    const REBVAL *code,
    REBFLGS flags
) {
    if (!IS_BLOCK(spec) || !IS_BLOCK(code))
        fail (Error_Bad_Func_Def(spec, code));

    REBFUN *fun = Make_Function(
        Make_Paramlist_Managed_May_Fail(spec, flags),
        &Plain_Dispatcher,
        NULL // no underlying function, this is fundamental
    );

    // We need to copy the body in order to relativize its references to
    // args and locals to refer to the parameter list.  Future implementations
    // might be able to "image" the bindings virtually, and not require this
    // copy if the input code is read-only.
    //
    REBARR *body_array =
        (VAL_ARRAY_LEN_AT(code) == 0)
            ? EMPTY_ARRAY // just reuse empty array if empty, no copy
            : Copy_And_Bind_Relative_Deep_Managed(
                code,
                FUNC_PARAMLIST(fun),
                TS_ANY_WORD
            );

    // We need to do a raw initialization of this block RELVAL because it is
    // relative to a function.  (Val_Init_Block assumes all specific values)
    //
    RELVAL *body = FUNC_BODY(fun);
    VAL_RESET_HEADER(body, REB_BLOCK);
    INIT_VAL_ARRAY(body, body_array);
    VAL_INDEX(body) = 0;
    SET_VAL_FLAG(body, VALUE_FLAG_RELATIVE);
    INIT_RELATIVE(body, fun);

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
        || GET_ARR_FLAG(VAL_ARRAY(spec), SERIES_FLAG_LEGACY)
        || GET_ARR_FLAG(VAL_ARRAY(code), SERIES_FLAG_LEGACY)
    ) {
        SET_VAL_FLAG(FUNC_VALUE(fun), FUNC_FLAG_LEGACY);
    }
#endif

#if !defined(NDEBUG)
    if (LEGACY(OPTIONS_MUTABLE_FUNCTION_BODIES))
        return fun; // don't run protection code below
#endif

    // All the series inside of a function body are "relatively bound".  This
    // means that there's only one copy of the body, but the series handle
    // is "viewed" differently based on which call it represents.  Though
    // each of these views compares uniquely, there's only one series behind
    // it...hence the series must be read only to keep modifying a view
    // that seems to have one identity but then affecting another.
    //
    // !!! The above is true in the specific-binding branch, but the rule
    // is applied to pre-specific-binding to prepare it for that future.
    //
    // !!! This protection needs to be system level, as the user is able to
    // unprotect conventional protection via UNPROTECT.
    //
    Protect_Series(
        ARR_SERIES(VAL_ARRAY(body)),
        0, // start protection at index 0
        FLAGIT(PROT_DEEP) | FLAGIT(PROT_SET)
    );
    assert(GET_ARR_FLAG(VAL_ARRAY(body), SERIES_FLAG_LOCKED));
    Unmark_Array(VAL_ARRAY(body));

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

    // In order to have the frame survive the call to MAKE and be
    // returned to the user it can't be stack allocated, because it
    // would immediately become useless.  Allocate dynamically.
    //
    REBARR *varlist = Make_Array(ARR_LEN(FUNC_PARAMLIST(func)));
    SET_ARR_FLAG(varlist, ARRAY_FLAG_CONTEXT_VARLIST);
    SET_ARR_FLAG(varlist, SERIES_FLAG_FIXED_SIZE);

    // Fill in the rootvar information for the context canon REBVAL
    //
    REBVAL *var = SINK(ARR_HEAD(varlist));
    VAL_RESET_HEADER(var, REB_FRAME);
    var->payload.any_context.varlist = varlist;

    // We can reuse the paramlist we're given, but note in the case of
    // definitional RETURN and LEAVE we have to stow the `binding` field
    // in the context, since the single archetype paramlist does not hold
    // enough information to know where to return *to*.
    //
    INIT_CTX_KEYLIST_SHARED(AS_CONTEXT(varlist), FUNC_PARAMLIST(func));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(AS_CONTEXT(varlist)));

    // !!! The frame will never have stack storage if created this
    // way, because we return it...and it would be of no use if the
    // stackvars were empty--they could not be filled.  However it
    // will have an associated call if it is run.  We don't know what
    // that call pointer will be so NULL is put in for now--but any
    // extant FRAME! values of this type will have to use stack
    // walks to find the pointer (possibly recaching in values.)
    //
    INIT_CONTEXT_FRAME(AS_CONTEXT(varlist), NULL);
    CTX_VALUE(AS_CONTEXT(varlist))->extra.binding = value->extra.binding;
    ++var;

    // A FRAME! defaults all args and locals to not being set.  If the frame
    // is then used as the storage for a function specialization, unset
    // vars indicate *unspecialized* arguments...not <opt> ones.  (This is
    // a good argument for not making <opt> have meaning that is interesting
    // to APPLY or SPECIALIZE cases, but to revoke the function's effects.
    //
    REBCNT n;
    for (n = 1; n <= FUNC_NUM_PARAMS(func); ++n, ++var)
        SET_VOID(var);

    SET_END(var);
    SET_ARRAY_LEN(varlist, ARR_LEN(FUNC_PARAMLIST(func)));

    return AS_CONTEXT(varlist);
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
) {
    assert(out != specializee);

    REBFUN *previous; // a previous specialization (if any)
    REBFUN *underlying = Underlying_Function(&previous, specializee);

    REBCTX *exemplar;

    if (previous) {
        //
        // Specializing a specialization is ultimately just a specialization
        // of the innermost function being specialized.  (Imagine specializing
        // a specialization of APPEND, to the point where it no longer takes
        // any parameters.  Nevertheless, the frame being stored and invoked
        // needs to have as many parameters as APPEND has.  The frame must be
        // be built for the code ultimately being called--and specializations
        // have no code of their own.)

        exemplar = VAL_CONTEXT(FUNC_BODY(previous));
        REBARR *varlist = Copy_Array_Deep_Managed(
            CTX_VARLIST(exemplar), SPECIFIED
        );
        SET_ARR_FLAG(varlist, ARRAY_FLAG_CONTEXT_VARLIST);
        INIT_CTX_KEYLIST_SHARED(AS_CONTEXT(varlist), CTX_KEYLIST(exemplar));

        exemplar = AS_CONTEXT(varlist); // okay, now make exemplar our copy
        CTX_VALUE(exemplar)->payload.any_context.varlist = varlist;
    }
    else {
        // An initial specialization is responsible for making a frame out
        // of the function's paramlist.  Frame vars default void.
        //
        exemplar = Make_Frame_For_Function(FUNC_VALUE(underlying));
        MANAGE_ARRAY(CTX_VARLIST(exemplar));
    }

    // Archetypal frame values can't have exit bindings (would write paramlist)
    //
    assert(VAL_BINDING(CTX_VALUE(exemplar)) == NULL);

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
    {
        PUSH_GUARD_ARRAY(CTX_VARLIST(exemplar));

        if (DO_VAL_ARRAY_AT_THROWS(out, block)) {
            DROP_GUARD_ARRAY(CTX_VARLIST(exemplar));
            return TRUE;
        }

        DROP_GUARD_ARRAY(CTX_VARLIST(exemplar));
    }

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
    }

    REBARR *paramlist = Pop_Stack_Values(dsp_orig);
    MANAGE_ARRAY(paramlist);

    RELVAL *rootparam = ARR_HEAD(paramlist);
    rootparam->payload.function.paramlist = paramlist;

    REBFUN *fun = Make_Function(
        paramlist,
        &Specializer_Dispatcher,
        underlying // cache the underlying function pointer in the paramlist
    );

    // The "body" is the FRAME! value of the specialization.  Though we may
    // not be able to touch the keylist of that frame to update the "archetype"
    // binding, we can patch this cell in the "body array" to hold it.
    //
    *FUNC_BODY(fun) = *CTX_VALUE(exemplar);
    assert(VAL_BINDING(FUNC_BODY(fun)) == VAL_BINDING(specializee));

    // See %sysobj.r for `specialized-meta:` object template

    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(ROOT_SPECIALIZED_META));

    assert(IS_VOID(CTX_VAR(meta, 1))); // no description by default
    *CTX_VAR(meta, 2) = *specializee;
    if (opt_specializee_name != NULL)
        Val_Init_Word(CTX_VAR(meta, 3), REB_WORD, opt_specializee_name);

    MANAGE_ARRAY(CTX_VARLIST(meta));
    ARR_SERIES(paramlist)->link.meta = meta;

    *out = *FUNC_VALUE(fun);
    assert(VAL_BINDING(out) == NULL);

    return FALSE;
}


//
//  Clonify_Function: C
// 
// The "Clonify" interface takes in a raw duplicate value that one
// wishes to mutate in-place into a full-fledged copy of the value
// it is a clone of.  This interface can be more efficient than a
// "source in, dest out" copy...and clarifies the dangers when the
// source and destination are the same.
//
void Clonify_Function(REBVAL *value)
{
    // !!! Conceptually the only types it currently makes sense to speak of
    // copying are functions and closures.  Though the concept is a little
    // bit "fuzzy"...the idea is that the series which are reachable from
    // their body series by a deep copy would be their "state".  Hence
    // as a function runs, its "state" can change.  One can thus define
    // a copy as snapshotting that "state".  This has been the classic
    // interpretation that Rebol has taken.

    // !!! However, in R3-Alpha a closure's "archetype" (e.g. the one made
    // by `clos [a] [print a]`) never operates on its body directly... it
    // is copied each time.  And there is no way at present to get a
    // reference to a closure "instance" (an ANY-FUNCTION value with the
    // copied body in it).  This has carried over to <durable> for now.

    // !!! This leaves only one function type that is mechanically
    // clonable at all... the non-durable FUNCTION!.  While the behavior is
    // questionable, for now we will suspend disbelief and preserve what
    // R3-Alpha did until a clear resolution.

    if (!IS_FUNCTION(value) || !IS_FUNCTION_PLAIN(value))
        return;

    if (IS_FUNC_DURABLE(VAL_FUNC(value)))
        return;

    // No need to modify the spec or header.  But we do need to copy the
    // identifying parameter series, so that the copied function has a
    // unique identity on the stack from the one it is copying.  Otherwise
    // two calls on the stack would be seen as recursions of the same
    // function, sharing each others "stack relative locals".

    REBFUN *original_fun = VAL_FUNC(value);

    // Ordinary copying would need to derelatavize all the relative values,
    // but copying the function to make it the body of another function
    // requires it to be "re-relativized"--all the relative references that
    // indicated the original function have to be changed to indicate the
    // new function.
    //
    REBARR *paramlist = Copy_Array_Shallow(
        FUNC_PARAMLIST(original_fun),
        SPECIFIED
    );
    MANAGE_ARRAY(paramlist);
    ARR_HEAD(paramlist)->payload.function.paramlist = paramlist;

    REBFUN *new_fun = Make_Function(
        paramlist,
        &Plain_Dispatcher,
        NULL // no underlying function, this is fundamental
    );

    // !!! Meta: copy, inherit?
    //
    ARR_SERIES(paramlist)->link.meta = FUNC_META(original_fun);

    RELVAL *body = FUNC_BODY(new_fun);

    // Since we rebind the body, we need to instruct the Plain_Dispatcher
    // that it's o.k. to tell the frame lookup that it can find variables
    // under the "new paramlist".  However, in specific binding where
    // bodies are not copied, you would preserve the "underlying" paramlist
    // in this slot
    //
    VAL_RESET_HEADER(body, REB_BLOCK);
    INIT_VAL_ARRAY(
        body,
        Copy_Rerelativized_Array_Deep_Managed(
            VAL_ARRAY(FUNC_BODY(original_fun)),
            original_fun,
            AS_FUNC(paramlist)
        )
    );
    VAL_INDEX(body) = 0;

    // Remap references in the body from the original function to new

    SET_VAL_FLAG(body, VALUE_FLAG_RELATIVE);
    INIT_RELATIVE(body, AS_FUNC(paramlist));

    *value = *FUNC_VALUE(new_fun);
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
REB_R Action_Dispatcher(struct Reb_Frame *f)
{
    Eval_Natives++;

    enum Reb_Kind type = VAL_TYPE(FRM_ARG(f, 1));

    REBACT subdispatch = Value_Dispatch[TO_0_FROM_KIND(type)];
    if (subdispatch == NULL)
        fail (Error_Illegal_Action(
            type, STR_SYMBOL(VAL_WORD_CANON(FUNC_BODY(f->func)))
        ));

    return subdispatch(f, STR_SYMBOL(VAL_WORD_CANON(FUNC_BODY(f->func))));
}


//
//  Plain_Dispatcher: C
//
REB_R Plain_Dispatcher(struct Reb_Frame *f)
{
    // In specific binding, we must always reify the frame and get it handed
    // over to the GC when calling user functions.  This is "costly" but
    // essential.
    //
    REBCTX *frame_ctx = Context_For_Frame_May_Reify_Managed(f);

    Eval_Functions++;

    RELVAL *body = FUNC_BODY(f->func);
    assert(IS_BLOCK(body) && IS_RELATIVE(body) && VAL_INDEX(body) == 0);

    REB_R r;
    if (Do_At_Throws(f->out, VAL_ARRAY(body), VAL_INDEX(body), frame_ctx))
        r = R_OUT_IS_THROWN;
    else
        r = R_OUT;

    return r;
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
REB_R Specializer_Dispatcher(struct Reb_Frame *f)
{
    REBVAL *exemplar = KNOWN(FUNC_BODY(f->func));
    f->func = VAL_FUNC(CTX_FRAME_FUNC_VALUE(VAL_CONTEXT(exemplar)));
    f->binding = VAL_BINDING(exemplar);

    return R_REDO;
}


//
//  Hijacker_Dispatcher: C
//
// A hijacker keeps the parameter list and layout, plus identity, of another
// function.  But instead of running that function's body, it maps the
// parameters into its own body.  It does this by actually mutating the
// contents of the shared body series that is held by all the instances
// of the function.
//
// To avoid its mechanical disruption from causing harm to any running
// instances, all function "bodies" must reserve their [0] slot for the
// hijacker.
//
REB_R Hijacker_Dispatcher(struct Reb_Frame *f)
{
    // Whatever was initially in the body of the function
    RELVAL *hook = FUNC_BODY(f->func);

    if (IS_BLANK(hook)) // blank hijacking allows capture, but nothing to run
        fail (Error(RE_HIJACK_BLANK));

    assert(IS_FUNCTION(hook));

    if (Redo_Func_Throws(f, VAL_FUNC(hook)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  Adapter_Dispatcher: C
//
REB_R Adapter_Dispatcher(struct Reb_Frame *f)
{
    REBCTX *frame_ctx = Context_For_Frame_May_Reify_Managed(f);

    RELVAL *adaptation = FUNC_BODY(f->func);
    assert(ARR_LEN(VAL_ARRAY(adaptation)) == 2);

    RELVAL* prelude = VAL_ARRAY_AT_HEAD(adaptation, 0);
    REBVAL* adaptee = KNOWN(VAL_ARRAY_AT_HEAD(adaptation, 1));

    // !!! With specific binding, we could slip the adapter a specifier for
    // the underlying function.  But until then, it looks at the stack.  The
    // f->func has to match what it's looking for that it bound to--which is
    // the underlying function.

    REBFUN *specializer;
    REBFUN *underlying = Underlying_Function(&specializer, adaptee);

    // The first thing to do is run the prelude code, which may throw.  If it
    // does throw--including a RETURN--that means the adapted function will
    // not be run.
    //
    if (Do_At_Throws(f->out, VAL_ARRAY(prelude), VAL_INDEX(prelude), frame_ctx))
        return R_OUT_IS_THROWN;

    // We have to run a type-checking sweep, to make sure the state of the
    // arguments is legal for the function.  Note that in particular,
    // a native function makes assumptions that the bit patterns are correct
    // for the set of types it takes...and most would crash the interpreter
    // if given a cell with an unexpected type in it.
    //
    // Notice also that the underlying params, though they must match the
    // order and symbols, may skip some due to specialization.
    //
    REBVAL *arg_save = f->arg;
    f->param = FUNC_PARAMS_HEAD(underlying);

    // We have to enumerate a frame as big as the underlying function, but
    // the adaptee may have fewer parameters due to specialization.  (In the
    // future, it may also subset the typesets accepted.)

    REBVAL *adaptee_param = FUNC_PARAMS_HEAD(VAL_FUNC(adaptee));

    for (; NOT_END(f->param); ++f->arg, ++f->param) {
        while (!SAME_STR(
            VAL_PARAM_CASED(adaptee_param), VAL_PARAM_CASED(f->param)
        )){
            ++adaptee_param;
        }

        // !!! In the future, it may be possible for adaptations to take
        // different types than the function they adapt (or perhaps just a
        // subset of those types)
        //
        assert(VAL_TYPESET_BITS(adaptee_param) == VAL_TYPESET_BITS(f->param));

        enum Reb_Param_Class pclass = VAL_PARAM_CLASS(adaptee_param);
        assert(pclass == VAL_PARAM_CLASS(adaptee_param));

        switch (pclass) {
        case PARAM_CLASS_LOCAL:
            SET_VOID(f->arg); // cheaper than checking
            break;

        case PARAM_CLASS_RETURN:
            assert(VAL_PARAM_SYM(f->param) == SYM_RETURN);

            if (!GET_VAL_FLAG(adaptee, FUNC_FLAG_RETURN)) {
                SET_VOID(f->arg); // may be another adapter, filled in later
                break;
            }

            *f->arg = *NAT_VALUE(return);

            if (f->varlist) // !!! in specific binding, always for Plain
                f->arg->extra.binding = f->varlist;
            else
                f->arg->extra.binding = FUNC_PARAMLIST(f->func);
            break;

        case PARAM_CLASS_LEAVE:
            assert(VAL_PARAM_SYM(f->param) == SYM_LEAVE);

            if (!GET_VAL_FLAG(FUNC_VALUE(f->func), FUNC_FLAG_LEAVE)) {
                SET_VOID(f->arg); // may be adapter, and filled in later
                break;
            }

            *f->arg = *NAT_VALUE(leave);

            if (f->varlist) // !!! in specific binding, always for Plain
                f->arg->extra.binding = f->varlist;
            else
                f->arg->extra.binding = FUNC_PARAMLIST(f->func);
            break;

        case PARAM_CLASS_REFINEMENT:
            if (!IS_LOGIC(f->arg))
                fail (Error_Non_Logic_Refinement(f));
            break;

        case PARAM_CLASS_HARD_QUOTE:
        case PARAM_CLASS_SOFT_QUOTE:
        case PARAM_CLASS_NORMAL:
            if (!TYPE_CHECK(adaptee_param, VAL_TYPE(f->arg)))
                fail (Error_Arg_Type(
                    FRM_LABEL(f), adaptee_param, VAL_TYPE(f->arg)
                ));
            break;

        default:
            assert(FALSE);
        }
    }

    f->arg = arg_save;

    f->func = VAL_FUNC(adaptee);
    f->binding = VAL_BINDING(adaptee);
    return R_REDO; // Have Do_Core run the adaptee updated into f->func
}


//
//  Chainer_Dispatcher: C
//
REB_R Chainer_Dispatcher(struct Reb_Frame *f)
{
    REBVAL *pipeline = KNOWN(FUNC_BODY(f->func)); // array of functions

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
    f->func = VAL_FUNC(value);
    f->binding = VAL_BINDING(value);

    return R_REDO;
}


//
//  func: native [
//  
//  "Defines a user function with given spec and body."
//  
//      spec [block!]
//          {Help string (opt) followed by arg words (and opt type + string)}
//      body [block!]
//          "The body block of the function"
//  ]
//
REBNATIVE(func)
//
// Native optimized implementation of a "definitional return" function
// generator.  See comments on Make_Function_May_Fail for full notes.
{
    PARAM(1, spec);
    PARAM(2, body);

    REBFUN *fun = Make_Plain_Function_May_Fail(
        ARG(spec), ARG(body), MKF_RETURN | MKF_LEAVE | MKF_KEYWORDS
    );

    *D_OUT = *FUNC_VALUE(fun);
    return R_OUT;
}


//
//  proc: native [
//
//  "Defines a user function with given spec and body and no return result."
//
//      spec [block!]
//          {Help string (opt) followed by arg words (and opt type + string)}
//      body [block!]
//          "The body block of the function, use LEAVE to exit"
//  ]
//
REBNATIVE(proc)
//
// Short for "PROCedure"; inspired by the Pascal language's discernment in
// terminology of a routine that returns a value vs. one that does not.
// Provides convenient interface similar to FUNC that will not accidentally
// leak values to the caller.
{
    PARAM(1, spec);
    PARAM(2, body);

    REBFUN *fun = Make_Plain_Function_May_Fail(
        ARG(spec), ARG(body), MKF_LEAVE | MKF_PUNCTUATES | MKF_KEYWORDS
    );

    *D_OUT = *FUNC_VALUE(fun);
    return R_OUT;
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
    REBVAL adjusted = *value;

    if (ANY_WORD(value)) {
        *opt_name_out = VAL_WORD_CASED(value);
        VAL_SET_TYPE_BITS(&adjusted, REB_GET_WORD);
    }
    else if (ANY_PATH(value)) {
        //
        // In theory we could get a symbol here, assuming we only do non
        // evaluated GETs.  Not implemented at the moment.
        //
        *opt_name_out = NULL;
        VAL_SET_TYPE_BITS(&adjusted, REB_GET_PATH);
    }
    else {
        *opt_name_out = NULL;
        *out = *value;
        return;
    }

    if (EVAL_VALUE_THROWS(out, &adjusted)) {
        //
        // !!! GET_PATH should not evaluate GROUP!, and hence shouldn't be
        // able to throw.  TBD.
        //
        fail (Error_No_Catch_For_Throw(out));
    }
}


//
//  specialize: native [
//
//  {Create a new function through partial or full specialization of another}
//
//      value [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Definition for FRAME! fields for args and refinements}
//  ]
//
REBNATIVE(specialize)
{
    PARAM(1, value);
    PARAM(2, def);

    REBSTR *opt_name;

    // We don't limit to taking a FUNCTION! value directly, because that loses
    // the symbol (for debugging, errors, etc.)  If caller passes a WORD!
    // then we lookup the variable to get the function, but save the symbol.
    //
    REBVAL specializee;
    Get_If_Word_Or_Path_Arg(&specializee, &opt_name, ARG(value));

    if (!IS_FUNCTION(&specializee))
        fail (Error(RE_APPLY_NON_FUNCTION, ARG(value))); // for APPLY too

    if (Specialize_Function_Throws(D_OUT, &specializee, opt_name, ARG(def)))
        return R_OUT_IS_THROWN;

    return R_OUT;
}


//
//  chain: native [
//
//  {Create a processing pipeline of functions that consume the last's result}
//
//      pipeline [block!]
//      /only
//  ]
//
REBNATIVE(chain)
{
    PARAM(1, pipeline);
    REFINE(2, only);

    REBVAL *out = D_OUT; // plan ahead for factoring into Chain_Function(out..

    assert(!REF(only)); // not written yet

    REBVAL *pipeline = ARG(pipeline);
    REBARR *chainees;
    if (REF(only)) {
        chainees = COPY_ANY_ARRAY_AT_DEEP_MANAGED(pipeline);
    }
    else {
        if (Reduce_Array_Throws(
            out,
            VAL_ARRAY(pipeline),
            VAL_INDEX(pipeline),
            VAL_SPECIFIER(pipeline),
            FALSE
        )) {
            return R_OUT_IS_THROWN;
        }
        chainees = VAL_ARRAY(out); // should be all specific values
        ASSERT_ARRAY_MANAGED(chainees);
    }

    REBVAL *first = KNOWN(ARR_HEAD(chainees));

    // !!! Current validation is that all are functions.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    REBVAL *check = first;
    while (NOT_END(check)) {
        if (!IS_FUNCTION(check))
            fail (Error_Invalid_Arg(check));
        ++check;
    }

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the first function in the chain.  It's
    // [0] element must identify the function we're creating vs the original,
    // however.
    //
    REBARR *paramlist = Copy_Array_Shallow(
        VAL_FUNC_PARAMLIST(ARR_HEAD(chainees)), SPECIFIED
    );
    ARR_HEAD(paramlist)->payload.function.paramlist = paramlist;
    MANAGE_ARRAY(paramlist);

    REBFUN *specializer;
    REBFUN *underlying = Underlying_Function(&specializer, first);

    REBFUN *fun = Make_Function(
        paramlist,
        &Chainer_Dispatcher,
        specializer != NULL ? specializer : underlying // cache in paramlist
    );

    // "body" is the chainees array, available to the dispatcher when called
    //
    Val_Init_Block(FUNC_BODY(fun), chainees);

    // See %sysobj.r for `specialized-meta:` object template

    REBVAL *std_meta = Get_System(SYS_STANDARD, STD_CHAINED_META);
    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(std_meta));

    assert(IS_VOID(CTX_VAR(meta, SELFISH(1)))); // no description by default
    Val_Init_Block(CTX_VAR(meta, SELFISH(2)), chainees);
    //
    // !!! There could be a system for preserving names in the chain, by
    // accepting lit-words instead of functions--or even by reading the
    // GET-WORD!s in the block.  Consider for the future.
    //
    assert(IS_VOID(CTX_VAR(meta, SELFISH(3))));

    MANAGE_ARRAY(CTX_VARLIST(meta));
    ARR_SERIES(paramlist)->link.meta = meta;

    *D_OUT = *FUNC_VALUE(fun);
    assert(VAL_BINDING(D_OUT) == NULL);

    return R_OUT;
}


//
//  adapt: native [
//
//  {Create a variant of a function that preprocesses its arguments}
//
//      adaptee [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      prelude [block!]
//          {Code to run in constructed frame before adapted function runs}
//  ]
//
REBNATIVE(adapt)
{
    PARAM(1, adaptee);
    PARAM(2, prelude);

    REBVAL *adaptee = ARG(adaptee);

    REBSTR *opt_adaptee_name;
    Get_If_Word_Or_Path_Arg(D_OUT, &opt_adaptee_name, adaptee);
    if (!IS_FUNCTION(D_OUT))
        fail (Error(RE_APPLY_NON_FUNCTION, adaptee));

    *adaptee = *D_OUT;

    // For the binding to be correct, the indices that the words use must be
    // the right ones for the frame pushed.  So if you adapt a specialization
    // that has one parameter, and the function that underlies that has
    // 10 parameters and the one parameter you're adapting to is it's 10th
    // and not its 1st...that has to be taken into account.
    //
    // Hence you must bind relative to that deeper function...e.g. the function
    // behind the frame of the specialization which gets pushed.
    //
    REBFUN *specializer;
    REBFUN *underlying = Underlying_Function(&specializer, adaptee);

    // !!! In a future branch it may be possible that specific binding allows
    // a read-only input to be "viewed" with a relative binding, and no copy
    // would need be made if input was R/O.  For now, we copy to relativize.
    //
    REBARR *prelude = Copy_And_Bind_Relative_Deep_Managed(
        ARG(prelude),
        FUNC_PARAMLIST(underlying),
        TS_ANY_WORD
    );

    // The paramlist needs to be unique to designate this function, but
    // will be identical typesets to the original.  It's [0] element must
    // identify the function we're creating vs the original, however.
    //
    REBARR *paramlist = Copy_Array_Shallow(
        VAL_FUNC_PARAMLIST(adaptee), SPECIFIED
    );
    ARR_HEAD(paramlist)->payload.function.paramlist = paramlist;
    MANAGE_ARRAY(paramlist);

    REBFUN *fun = Make_Function(
        paramlist,
        &Adapter_Dispatcher,
        specializer != NULL ? specializer : underlying // cache in paramlist
    );

    // We need to store the 2 values describing the adaptation so that the
    // dispatcher knows what to do when it gets called and inspects FUNC_BODY.
    //
    // [0] is the prelude BLOCK!, [1] is the FUNCTION! we've adapted.
    //
    REBARR *adaptation = Make_Array(2);

    REBVAL *block = Alloc_Tail_Array(adaptation);
    VAL_RESET_HEADER(block, REB_BLOCK);
    INIT_VAL_ARRAY(block, prelude);
    VAL_INDEX(block) = 0;
    SET_VAL_FLAG(block, VALUE_FLAG_RELATIVE);
    INIT_RELATIVE(block, underlying);

    Append_Value(adaptation, adaptee);

    RELVAL *body = FUNC_BODY(fun);
    VAL_RESET_HEADER(body, REB_BLOCK);
    INIT_VAL_ARRAY(body, adaptation);
    VAL_INDEX(body) = 0;
    SET_VAL_FLAG(body, VALUE_FLAG_RELATIVE);
    INIT_RELATIVE(body, underlying);
    MANAGE_ARRAY(adaptation);

    // See %sysobj.r for `specialized-meta:` object template

    REBVAL *example = Get_System(SYS_STANDARD, STD_ADAPTED_META);

    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(example));
    assert(IS_VOID(CTX_VAR(meta, SELFISH(1)))); // no description by default
    *CTX_VAR(meta, SELFISH(2)) = *adaptee;
    if (opt_adaptee_name != NULL)
        Val_Init_Word(CTX_VAR(meta, SELFISH(3)), REB_WORD, opt_adaptee_name);

    MANAGE_ARRAY(CTX_VARLIST(meta));
    ARR_SERIES(paramlist)->link.meta = meta;

    *D_OUT = *FUNC_VALUE(fun);
    assert(VAL_BINDING(D_OUT) == NULL);

    return R_OUT;
}


//
//  hijack: native [
//
//  {Cause all existing references to a function to invoke another function.}
//
//      victim [function! any-word! any-path!]
//          {Function value whose references are to be affected.}
//      hijacker [function! any-word! any-path! blank!]
//          {The function to run in its place or BLANK! to extract prior code.}
//  ]
//
REBNATIVE(hijack)
//
// !!! Should the parameters be checked for baseline compatibility, or just
// let all failures happen at the moment of trying to run the hijack?
// As it is, one might not require a perfectly compatible interface,
// and be tolerant if the refinements don't line up...just fail if any
// case of trying to use unaligned refinements happens.
//
{
    PARAM(1, victim);
    PARAM(2, hijacker);

    REBVAL victim_value;
    REBSTR *opt_victim_name;
    Get_If_Word_Or_Path_Arg(
        &victim_value, &opt_victim_name, ARG(victim)
    );
    REBVAL *victim = &victim_value;
    if (!IS_FUNCTION(victim))
        fail (Error(RE_MISC));

    REBVAL hijacker_value;
    REBSTR *opt_hijacker_name;
    Get_If_Word_Or_Path_Arg(
        &hijacker_value, &opt_hijacker_name, ARG(hijacker)
    );
    REBVAL *hijacker = &hijacker_value;
    if (!IS_FUNCTION(hijacker) && !IS_BLANK(hijacker))
        fail (Error(RE_MISC));

    // !!! Should hijacking a function with itself be a no-op?  One could make
    // an argument from semantics that the effect of replacing something with
    // itself is not to change anything, but erroring may give a sanity check.
    //
    if (!IS_BLANK(hijacker) && VAL_FUNC(victim) == VAL_FUNC(hijacker))
        fail (Error(RE_MISC));

    if (IS_FUNCTION_HIJACKER(victim) && IS_BLANK(VAL_FUNC_BODY(victim))) {
        //
        // If the victim is a "blank hijackee", it was generated by a previous
        // hijack call.  This was likely for the purposes of getting a proxy
        // for the function to use in the hijacker's implementation itself.
        //
        // We don't bother copying the paramlist to proxy it again--just poke
        // the value into the paramlist directly, and return blank to signify
        // that no new proxy could be made.

        if (IS_BLANK(hijacker))
            fail (Error(RE_MISC)); // !!! Allow re-blanking a blank?

        SET_BLANK(D_OUT);
    }
    else {
        // For non-blank victims, the return value will be a proxy for that
        // victim.  This proxy must have a different paramlist from the
        // original victim being hijacked (otherwise, calling it would call
        // the hijacker too).  So it's a copy.

        REBFUN *victim_underlying
            = ARR_SERIES(victim->payload.function.paramlist)->misc.underlying;

        REBARR *proxy_paramlist = Copy_Array_Deep_Managed(
            victim->payload.function.paramlist,
            SPECIFIED // !!! Note: not actually "deep", just typesets
        );
        ARR_HEAD(proxy_paramlist)->payload.function.paramlist
            = proxy_paramlist;
        ARR_SERIES(proxy_paramlist)->link.meta = VAL_FUNC_META(victim);

        // If the proxy had a body, then that body will be bound relative
        // to the original paramlist that's getting hijacked.  So when the
        // proxy is called, we want the frame pushed to be relative to
        // whatever underlied the function...even if it was foundational
        // so `victim_underlying = VAL_FUNC(victim)`

        REBFUN *proxy = Make_Function(
            proxy_paramlist,
            FUNC_DISPATCHER(VAL_FUNC(victim)),
            victim_underlying
        );

        // The victim's body is overwritten below to hold the hijacker.  Copy
        // the REBVAL bits first.

        *FUNC_BODY(proxy) = *VAL_FUNC_BODY(victim);

        *D_OUT = *FUNC_VALUE(proxy);
        D_OUT->extra.binding = VAL_BINDING(victim);

        REBFUN *specializer;
        Underlying_Function(&specializer, D_OUT);
    }

    // With the return value settled, do the actual hijacking.  The "body"
    // payload of a hijacker is the replacement function value itself.
    //
    // Note we don't want to disrupt the underlying function from whatever it
    // was before, because derived compositions cached that.  It will not
    // match the hijacker, so it won't be able to directly use the frame
    // which is built, and will have to build a new frame in the dispatcher.

    *VAL_FUNC_BODY(victim) = *hijacker;
    ARR_SERIES(victim->payload.function.body_holder)->misc.dispatcher
        = &Hijacker_Dispatcher;

    victim->extra.binding = NULL; // old exit binding extracted for proxy

    *ARR_HEAD(VAL_FUNC_PARAMLIST(victim)) = *victim; // update rootparam

    // Update the meta information on the function to indicate it's hijacked
    // See %sysobj.r for `hijacked-meta:` object template

    REBVAL *std_meta = Get_System(SYS_STANDARD, STD_HIJACKED_META);
    REBCTX *meta = Copy_Context_Shallow(VAL_CONTEXT(std_meta));

    assert(IS_VOID(CTX_VAR(meta, SELFISH(1)))); // no description by default
    *CTX_VAR(meta, SELFISH(2)) = *D_OUT;
    if (opt_victim_name != NULL)
        Val_Init_Word(CTX_VAR(meta, SELFISH(3)), REB_WORD, opt_victim_name);

    MANAGE_ARRAY(CTX_VARLIST(meta));
    ARR_SERIES(VAL_FUNC_PARAMLIST(victim))->link.meta = meta;

    REBFUN *specializer;
    Underlying_Function(&specializer, victim);

    return R_OUT;
}


//
//  Apply_Frame_Core: C
//
// Work in progress to factor out common code used by DO and APPLY.  Needs
// to be streamlined.
//
// Expects the following Reb_Frame fields to be preloaded:
//
//    f->out (just valid pointer, pointed-to value can be garbage)
//    f->func
//    f->binding
//
// If opt_def is NULL, then f->data.context must be set
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
REB_R Apply_Frame_Core(struct Reb_Frame *f, REBSTR *label, REBVAL *opt_def)
{
    assert(IS_FUNCTION(f->gotten));

    f->eval_type = ET_FUNCTION;
    SET_FRAME_LABEL(f, label);

    // We pretend our "input source" has ended.
    //
    SET_FRAME_VALUE(f, END_CELL);
    f->index = 0;
    f->source.array = EMPTY_ARRAY;
    f->specifier = SPECIFIED;
    f->pending = NULL;

    f->dsp_orig = DSP;

    f->flags =
        DO_FLAG_NEXT
        | DO_FLAG_NO_LOOKAHEAD
        | DO_FLAG_NO_ARGS_EVALUATE
        | DO_FLAG_APPLYING;

    // !!! We have to push a call here currently because prior to specific
    // binding, the stack gets walked to resolve variables.   Hence in the
    // apply case, Do_Core doesn't do its own push to the frame stack.
    //
    PUSH_CALL(f);

#if !defined(NDEBUG)
    //
    // We may push a data chunk, which is one of the things the snapshot state
    // checks.  It also checks the top of stack, so that has to be set as well.
    // So this has to come before Push_Or_Alloc_Vars
    //
    SNAP_STATE(&f->state);
#endif

    // If applying an existing FRAME! there should be no need to push vars
    // for it...it should have its own space.
    //
    if (opt_def) {
        Push_Or_Alloc_Args_For_Underlying_Func(f);
    }
    else {
        // f->func and f->binding should already be set

        // !!! This form of execution raises a ton of open questions about
        // what to do if a frame is used more than once.  Function calls
        // are allowed to destroy their arguments and will contaminate the
        // pure locals.  We need to treat this as a "non-specializing
        // specialization", and push a frame.  The narrow case of frame
        // reuse needs to be contained to something that a function can only
        // do to itself--e.g. to facilitate tail recursion, because no caller
        // but the function itself understands the state of its locals in situ.
        //
        ASSERT_CONTEXT(AS_CONTEXT(f->varlist));
        f->stackvars = NULL;

        f->arg = FRM_ARGS_HEAD(f);
        f->param = FUNC_PARAMS_HEAD(f->func); // !!! Review
    }

    f->cell.subfeed = NULL;

    if (opt_def) {
        Bind_Values_Core(
            VAL_ARRAY_AT(opt_def),
            Context_For_Frame_May_Reify_Core(f),
            FLAGIT_KIND(REB_SET_WORD), // types to bind (just set-word!)
            0, // types to "add midstream" to binding as we go (nothing)
            BIND_DEEP
        );

        // Do the block into scratch space--we ignore the result (unless it is
        // thrown, in which case it must be returned.)
        //
        if (DO_VAL_ARRAY_AT_THROWS(f->out, opt_def)) {
            DROP_CALL(f);
            return R_OUT_IS_THROWN;
        }
    }

    f->refine = NULL;
    SET_END(f->out);

    Do_Core(f);

    if (THROWN(f->out))
        return R_OUT_IS_THROWN; // prohibits recovery from exits

    assert(IS_END(f->value)); // we started at END_FLAG, can only throw

    return R_OUT;
}


//
//  apply: native [
//
//  {Invoke a function with all required arguments specified.}
//
//      value [function! any-word! any-path!]
//          {Function or specifying word (preserves word name for debug info)}
//      def [block!]
//          {Frame definition block (will be bound and evaluated)}
//  ]
//
REBNATIVE(apply)
{
    PARAM(1, value);
    PARAM(2, def);

    REBVAL *def = ARG(def);

    struct Reb_Frame frame;
    struct Reb_Frame *f = &frame;

#if !defined(NDEBUG)
    RELVAL *first_def = VAL_ARRAY_AT(def);

    // !!! Because APPLY has changed, help warn legacy usages by alerting
    // if the first element of the block is not a SET-WORD!.  A BAR! can
    // subvert the warning: `apply :foo [| comment {This is a new APPLY} ...]`
    //
    if (NOT_END(first_def)) {
        if (!IS_SET_WORD(first_def) && !IS_BAR(first_def)) {
            fail (Error(RE_APPLY_HAS_CHANGED));
        }
    }
#endif

    // We don't limit to taking a FUNCTION! value directly, because that loses
    // the symbol (for debugging, errors, etc.)  If caller passes a WORD!
    // then we lookup the variable to get the function, but save the symbol.
    //
    REBSTR *name;
    Get_If_Word_Or_Path_Arg(D_OUT, &name, ARG(value));
    if (name == NULL)
        name = Canon(SYM___ANONYMOUS__); // Do_Core requires non-NULL symbol

    if (!IS_FUNCTION(D_OUT))
        fail (Error(RE_APPLY_NON_FUNCTION, ARG(value))); // for SPECIALIZE too

    f->gotten = D_OUT;
    f->out = D_OUT;

    return Apply_Frame_Core(f, name, def);
}


#if !defined(NDEBUG)

//
//  VAL_FUNC_Debug: C
//
REBFUN *VAL_FUNC_Debug(const RELVAL *v) {
    REBFUN *func = AS_FUNC(v->payload.function.paramlist);
    struct Reb_Value_Header v_header = v->header;
    struct Reb_Value_Header func_header = FUNC_VALUE(func)->header;

    assert(IS_FUNCTION(v));
    assert(func == AS_FUNC(FUNC_VALUE(func)->payload.function.paramlist));
    assert(GET_ARR_FLAG(FUNC_PARAMLIST(func), SERIES_FLAG_ARRAY));

    assert(VAL_FUNC_BODY(v) == FUNC_BODY(func));
    assert(VAL_FUNC_DISPATCHER(v) == FUNC_DISPATCHER(func));

    // set VALUE_FLAG_LINE on both headers for sake of comparison, we allow
    // it to be different from the value stored in frame.
    //
    // !!! Should formatting flags be moved into their own section, perhaps
    // the section currently known as "resv: reserved for future use"?
    //
    // We also set VALUE_FLAG_THROWN as that is not required to be sync'd
    // with the persistent value in the function.  This bit is deprecated
    // however, for many of the same reasons it's a nuisance here.
    //
    v_header.bits |= (
        VALUE_FLAG_LINE
        | VALUE_FLAG_THROWN
        | VALUE_FLAG_EVALUATED
    );
    func_header.bits |= (
        VALUE_FLAG_LINE
        | VALUE_FLAG_THROWN
        | VALUE_FLAG_EVALUATED
    );

    if (v_header.bits != func_header.bits) {
        //
        // If this happens, these help with debugging if stopped at breakpoint.
        //
        REBVAL *func_value = FUNC_VALUE(func);
        REBOOL has_return_value
            = GET_VAL_FLAG(v, FUNC_FLAG_RETURN);
        REBOOL has_return_func
            = GET_VAL_FLAG(func_value, FUNC_FLAG_RETURN);
        REBOOL has_leave_value
            = GET_VAL_FLAG(v, FUNC_FLAG_LEAVE);
        REBOOL has_leave_func
            = GET_VAL_FLAG(func_value, FUNC_FLAG_LEAVE);

        Debug_Fmt("Mismatch header bits found in FUNC_VALUE from payload");
        Panic_Array(FUNC_PARAMLIST(func));
    }

    return func;
}

#endif
