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
Array* List_Func_Words(const Cell* func, bool pure_locals)
{
    StackIndex base = TOP_INDEX;

    Value* param = VAL_ACT_PARAMS_HEAD(func);
    for (; NOT_END(param); param++) {
        if (Is_Param_Hidden(param)) // specialization hides parameters
            continue;

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
            if (not pure_locals)
                continue; // treat as invisible, e.g. for WORDS-OF

            kind = REB_SET_WORD;
            break;

        default:
            assert(false);
            DEAD_END;
        }

        Init_Any_Word(PUSH(), kind, Cell_Parameter_Symbol(param));
    }

    return Pop_Stack_Values(base);
}


//
//  List_Func_Typesets: C
//
// Return a block of function arg typesets.
// Note: skips 0th entry.
//
Array* List_Func_Typesets(Value* func)
{
    Array* array = Make_Array(VAL_ACT_NUM_PARAMS(func));
    Value* typeset = VAL_ACT_PARAMS_HEAD(func);

    for (; NOT_END(typeset); typeset++) {
        assert(Is_Typeset(typeset));

        Value* value = Copy_Cell(Alloc_Tail_Array(array), typeset);

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
Array* Make_Paramlist_Managed_May_Fail(
    const Value* spec,
    Flags flags
) {
    assert(Any_List(spec));

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

    StackIndex base = TOP_INDEX;
    assert(TOP == Data_Stack_At(base));

    StackIndex return_stackindex = 0;

    // As we go through the spec block, we push TYPESET! BLOCK! TEXT! triples.
    // These will be split out into separate arrays after the process is done.
    // The first slot of the paramlist needs to be the function canon value,
    // while the other two first slots need to be rootkeys.  Get the process
    // started right after a BLOCK! so it's willing to take a string for
    // the function description--it will be extracted from the slot before
    // it is turned into a rootkey for param_notes.
    //
    Init_Unreadable(PUSH());  // paramlist[0] will become ACT_ARCHETYPE()
    Copy_Cell(PUSH(), EMPTY_BLOCK);  // param_types[0] (to be OBJECT! canon)
    Copy_Cell(PUSH(), EMPTY_TEXT); // param_notes[0] (description, then canon)

    bool has_description = false;
    bool has_types = false;
    bool has_notes = false;

    enum Reb_Spec_Mode mode = SPEC_MODE_NORMAL;

    bool refinement_seen = false;

    const Cell* value = Cell_List_At(spec);

    while (NOT_END(value)) {
        const Cell* item = value; // "faked", e.g. <return> => RETURN:
        ++value; // go ahead and consume next

    //=//// STRING! FOR FUNCTION DESCRIPTION OR PARAMETER NOTE ////////////=//

        if (Is_Text(item)) {
            //
            // Consider `[<with> some-extern "description of that extern"]` to
            // be purely commentary for the implementation, and don't include
            // it in the meta info.
            //
            if (mode == SPEC_MODE_WITH)
                continue;

            if (Is_Typeset(TOP))
                Copy_Cell(PUSH(), EMPTY_BLOCK);  // need block in position

            if (Is_Block(TOP)) { // we're in right spot to push notes/title
                Init_Text(PUSH(), Copy_String_At_Len(item, -1));
            }
            else { // !!! A string was already pushed.  Should we append?
                assert(Is_Text(TOP));
                Init_Text(TOP, Copy_String_At_Len(item, -1));
            }

            if (TOP == Data_Stack_At(base + 3))
                has_description = true;
            else
                has_notes = true;

            continue;
        }

    //=//// TOP-LEVEL SPEC TAGS LIKE <local>, <with> etc. /////////////////=//

        if (Is_Tag(item) and (flags & MKF_KEYWORDS)) {
            if (0 == Compare_String_Vals(item, Root_With_Tag, true)) {
                mode = SPEC_MODE_WITH;
                continue;
            }
            else if (0 == Compare_String_Vals(item, Root_Local_Tag, true)) {
                mode = SPEC_MODE_LOCAL;
                continue;
            }
            else
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
        }

    //=//// BLOCK! OF TYPES TO MAKE TYPESET FROM (PLUS PARAMETER TAGS) ////=//

        if (Is_Block(item)) {
            if (  // legacy behavior: `return: [~]` erases return result
                VAL_ARRAY_LEN_AT(item) == 1
                and Is_Word(Cell_List_At(item))
                and Cell_Word_Id(Cell_List_At(item)) == SYM_TILDE_1
            ){
                header_bits |= CELL_FLAG_ACTION_TRASHER;  // Eraser_Dispatcher()
            }

            if (Is_Block(TOP)) // two blocks of types!
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
            Value* typeset;
            if (Is_Typeset(TOP)) {
                Specifier* derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
                Init_Block(
                    PUSH(),
                    Copy_Array_At_Deep_Managed(
                        Cell_Array(item),
                        VAL_INDEX(item),
                        derived
                    )
                );

                typeset = TOP - 1;  // volatile if you PUSH()!
            }
            else {
                assert(Is_Text(TOP)); // !!! are blocks after notes good?

                if (IS_BLANK_RAW(TOP - 2)) {
                    //
                    // No parameters pushed, e.g. func [[integer!] {<-- bad}]
                    //
                    fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
                }

                assert(Is_Typeset(TOP - 2));
                typeset = TOP - 2;

                assert(Is_Block(TOP - 1));
                if (Cell_Array(TOP - 1) != EMPTY_ARRAY)
                    fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

                Specifier* derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
                Init_Block(
                    TOP - 1,
                    Copy_Array_At_Deep_Managed(
                        Cell_Array(item),
                        VAL_INDEX(item),
                        derived
                    )
                );
            }

            // Turn block into typeset for parameter at current index.
            // Leaves VAL_TYPESET_SYM as-is.
            //
            Specifier* derived = Derive_Specifier(VAL_SPECIFIER(spec), item);
            Update_Typeset_Bits_Core(
                typeset,
                VAL_ARRAY_HEAD(item),
                derived
            );

            // Refinements and refinement arguments cannot be specified as
            // ~null~.  Although refinement arguments may be void, they are
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

        if (not Any_Word(item))
            fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));

        // !!! If you say [<with> x /foo y] the <with> terminates and a
        // refinement is started.  Same w/<local>.  Is this a good idea?
        // Note that historically, help hides any refinements that appear
        // behind a /local, but this feature has no parallel in Ren-C.
        //
        if (mode != SPEC_MODE_NORMAL) {
            if (Is_Refinement(item)) {
                mode = SPEC_MODE_NORMAL;
            }
            else if (not Is_Word(item) and not Is_Set_Word(item))
                fail (Error_Bad_Func_Def_Core(item, VAL_SPECIFIER(spec)));
        }

        Symbol* canon = VAL_WORD_CANON(item);

        // In rhythm of TYPESET! BLOCK! TEXT! we want to be on a string spot
        // at the time of the push of each new typeset.
        //
        if (Is_Typeset(TOP))
            Copy_Cell(PUSH(), EMPTY_BLOCK);
        if (Is_Block(TOP))
            Copy_Cell(PUSH(), EMPTY_TEXT);
        assert(Is_Text(TOP));

        // Non-annotated arguments disallow ACTION!, NOTHING and NULL.  Not
        // having to worry about ACTION! and NULL means by default, code
        // does not have to worry about "disarming" arguments via GET-WORD!.
        // Also, keeping NULL a bit "prickly" helps discourage its use as
        // an input parameter...because it faces problems being used in
        // SPECIALIZE and other scenarios.
        //
        // Note there are currently two ways to get NULL: ~null~ and <end>.
        // If the typeset bits contain REB_MAX_NULLED, that indicates ~null~.
        // But Is_Param_Endable() indicates <end>.
        //
        Value* typeset = Init_Typeset(
            PUSH(),  // volatile if you PUSH() again
            (flags & MKF_Any_Value)
                ? TS_OPT_VALUE
                : TS_VALUE & ~(
                    FLAGIT_KIND(REB_ACTION)
                    | FLAGIT_KIND(REB_NOTHING)
                ),
            Cell_Word_Symbol(item) // don't canonize, see #2258
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
        if (Symbol_Id(canon) == SYM_RETURN) {
            if (return_stackindex != 0) {
                DECLARE_VALUE (word);
                Init_Word(word, canon);
                fail (Error_Dup_Vars_Raw(word)); // most dup checks done later
            }
            if (Is_Set_Word(item))
                return_stackindex = TOP_INDEX;  // RETURN: explicitly tolerated
            else
                flags &= ~(MKF_RETURN | MKF_FAKE_RETURN);
        }

        if (mode == SPEC_MODE_WITH and not Is_Set_Word(item)) {
            //
            // Because FUNC does not do any locals gathering by default, the
            // main purpose of <with> is for instructing it not to do the
            // definitional returns.  However, it also makes changing between
            // FUNC and FUNCTION more fluid.
            //
            // !!! If you write something like `func [x <with> x] [...]` that
            // should be sanity checked with an error...TBD.
            //
            DROP(); // forge the typeset, used in `definitional_return` case
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
            refinement_seen = true;
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

    // Go ahead and flesh out the TYPESET! BLOCK! TEXT! triples.
    //
    if (Is_Typeset(TOP))
        Copy_Cell(PUSH(), EMPTY_BLOCK);
    if (Is_Block(TOP))
        Copy_Cell(PUSH(), EMPTY_TEXT);
    assert((TOP_INDEX - base) % 3 == 0);  // must be a multiple of 3

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
        if (return_stackindex == 0) {  // no explicit RETURN: pure local
            //
            // While default arguments disallow ACTION!, NOTHING, and NULL...
            // they are allowed to return anything.  Generally speaking, the
            // checks are on the input side, not the output.
            //
            Init_Typeset(PUSH(), TS_OPT_VALUE, Canon(SYM_RETURN));
            INIT_VAL_PARAM_CLASS(TOP, PARAM_CLASS_RETURN);
            return_stackindex = TOP_INDEX;

            Copy_Cell(PUSH(), EMPTY_BLOCK);
            Copy_Cell(PUSH(), EMPTY_TEXT);
            // no need to move it--it's already at the tail position
        }
        else {
            Value* param = Data_Stack_At(return_stackindex);
            assert(VAL_PARAM_CLASS(param) == PARAM_CLASS_LOCAL);
            INIT_VAL_PARAM_CLASS(param, PARAM_CLASS_RETURN);

            // definitional_return handled specially when paramlist copied
            // off of the stack...
        }
        header_bits |= CELL_FLAG_ACTION_RETURN;
    }

    // Slots, which is length +1 (includes the rootvar or rootparam)
    //
    REBLEN num_slots = (TOP_INDEX - base) / 3;

    // If we pushed a typeset for a return and it's a native, it actually
    // doesn't want a RETURN: key in the frame in release builds.  We'll omit
    // from the copy.
    //
    if (return_stackindex != 0 and (flags & MKF_FAKE_RETURN))
        --num_slots;

    // There should be no more pushes past this point, so a stable pointer
    // into the stack for the definitional return can be found.
    //
    Value* definitional_return =
        return_stackindex == 0
            ? nullptr
            : Data_Stack_At(return_stackindex);

    // Must make the function "paramlist" even if "empty", for identity.
    //
    Array* paramlist = Make_Array_Core(num_slots, SERIES_MASK_ACTION);

    if (true) {
        Value* canon = RESET_CELL_EXTRA(
            Array_Head(paramlist),
            REB_ACTION,
            header_bits
        );
        canon->payload.action.paramlist = paramlist;
        INIT_BINDING(canon, UNBOUND);

        Value* dest = canon + 1;

        // We want to check for duplicates and a Binder can be used for that
        // purpose--but note that a fail() cannot happen while binders are
        // in effect UNLESS the BUF_COLLECT contains information to undo it!
        // There's no BUF_COLLECT here, so don't fail while binder in effect.
        //
        // (This is why we wait until the parameter list gathering process
        // is over to do the duplicate checks--it can fail.)
        //
        struct Reb_Binder binder;
        INIT_BINDER(&binder, nullptr);

        Symbol* duplicate = nullptr;

        Value* src = Data_Stack_At(base + 1) + 3;

        for (; src <= TOP; src += 3) {
            assert(Is_Typeset(src));
            if (not Try_Add_Binder_Index(&binder, Cell_Param_Canon(src), 1020))
                duplicate = Cell_Parameter_Symbol(src);

            if (definitional_return and src == definitional_return)
                continue;

            Copy_Cell(dest, src);
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
                Copy_Cell(dest, definitional_return);
                ++dest;
            }
        }

        // Must remove binder indexes for all words, even if about to fail
        //
        src = Data_Stack_At(base + 1) + 3;
        for (; src <= TOP; src += 3, ++dest) {
            if (
                Remove_Binder_Index_Else_0(&binder, Cell_Param_Canon(src))
                == 0
            ){
                assert(duplicate);
            }
        }

        SHUTDOWN_BINDER(&binder);

        if (duplicate) {
            DECLARE_VALUE (word);
            Init_Word(word, duplicate);
            fail (Error_Dup_Vars_Raw(word));
        }

        Term_Array_Len(paramlist, num_slots);
        Manage_Flex(paramlist);
    }

    //=///////////////////////////////////////////////////////////////////=//
    //
    // BUILD META INFORMATION OBJECT (IF NEEDED)
    //
    //=///////////////////////////////////////////////////////////////////=//

    // !!! See notes on ACTION-META in %sysobj.r

    VarList* meta = nullptr;

    if (has_description or has_types or has_notes)
        meta = Copy_Context_Shallow_Managed(Cell_Varlist(Root_Action_Meta));

    MISC(paramlist).meta = meta;

    // If a description string was gathered, it's sitting in the first string
    // slot, the third cell we pushed onto the stack.  Extract it if so.
    //
    if (has_description) {
        assert(Is_Text(Data_Stack_At(base + 3)));
        Copy_Cell(
            Varlist_Slot(meta, STD_ACTION_META_DESCRIPTION),
            Data_Stack_At(base + 3)
        );
    }

    // Only make `parameter-types` if there were blocks in the spec
    //
    if (has_types) {
        Array* types_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED
        );
        MISC(types_varlist).meta = nullptr;  // GC sees this, must initialize
        Tweak_Keylist_Of_Varlist_Shared(CTX(types_varlist), paramlist);

        Value* rootvar = RESET_CELL(Array_Head(types_varlist), REB_FRAME);
        rootvar->payload.any_context.varlist = types_varlist; // canon FRAME!
        rootvar->payload.any_context.phase = ACT(paramlist);
        INIT_BINDING(rootvar, UNBOUND);

        Value* dest = rootvar + 1;

        Value* src = Data_Stack_At(base + 2);
        src += 3;
        for (; src <= TOP; src += 3) {
            assert(Is_Block(src));
            if (definitional_return and src == definitional_return + 1)
                continue;

            if (VAL_ARRAY_LEN_AT(src) == 0)
                Init_Nulled(dest);
            else
                Copy_Cell(dest, src);
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
                Copy_Cell(
                    Varlist_Slot(meta, STD_ACTION_META_RETURN_TYPE),
                    &definitional_return[1]
                );
            }

            if (not (flags & MKF_FAKE_RETURN)) {
                Init_Nulled(dest); // clear the local RETURN: var's description
                ++dest;
            }
        }

        Term_Array_Len(types_varlist, num_slots);

        Init_Any_Context(
            Varlist_Slot(meta, STD_ACTION_META_PARAMETER_TYPES),
            REB_FRAME,
            CTX(types_varlist)
        );
    }

    // Only make `parameter-notes` if there were strings (besides description)
    //
    if (has_notes) {
        Array* notes_varlist = Make_Array_Core(
            num_slots,
            SERIES_MASK_CONTEXT | NODE_FLAG_MANAGED
        );
        MISC(notes_varlist).meta = nullptr;  // GC sees this, must initialize
        Tweak_Keylist_Of_Varlist_Shared(CTX(notes_varlist), paramlist);

        Value* rootvar = RESET_CELL(Array_Head(notes_varlist), REB_FRAME);
        rootvar->payload.any_context.varlist = notes_varlist; // canon FRAME!
        rootvar->payload.any_context.phase = ACT(paramlist);
        INIT_BINDING(rootvar, UNBOUND);

        Value* dest = rootvar + 1;

        Value* src = Data_Stack_At(base + 3);
        src += 3;
        for (; src <= TOP; src += 3) {
            assert(Is_Text(src));
            if (definitional_return and src == definitional_return + 2)
                continue;

            if (Flex_Len(Cell_Flex(src)) == 0)
                Init_Nulled(dest);
            else
                Copy_Cell(dest, src);
            ++dest;
        }

        if (definitional_return) {
            //
            // See remarks on the return type--the RETURN is documented in
            // the top-level META-OF, not the "incidentally" named RETURN
            // parameter in the list
            //
            if (Flex_Len(Cell_Flex(definitional_return + 2)) == 0)
                Init_Nulled(Varlist_Slot(meta, STD_ACTION_META_RETURN_NOTE));
            else {
                Copy_Cell(
                    Varlist_Slot(meta, STD_ACTION_META_RETURN_NOTE),
                    &definitional_return[2]
                );
            }

            if (not (flags & MKF_FAKE_RETURN)) {
                Init_Nulled(dest);
                ++dest;
            }
        }

        Term_Array_Len(notes_varlist, num_slots);

        Init_Frame(
            Varlist_Slot(meta, STD_ACTION_META_PARAMETER_NOTES),
            CTX(notes_varlist)
        );
    }

    // With all the values extracted from stack to array, restore stack pointer
    //
    Drop_Data_Stack_To(base);

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
REBLEN Find_Param_Index(Array* paramlist, Symbol* symbol)
{
    Symbol* canon = Canon_Symbol(symbol);  // don't recalculate each time

    Cell* param = Array_At(paramlist, 1);
    REBLEN len = Array_Len(paramlist);

    REBLEN n;
    for (n = 1; n < len; ++n, ++param) {
        if (
            symbol == Cell_Parameter_Symbol(param)
            or canon == Cell_Param_Canon(param)
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
//     const Value* Dispatcher(Level* L) {...}
//
// The REBACT returned is "archetypal" because individual REBVALs which hold
// the same REBACT may differ in a per-cell "binding".  (This is how one
// RETURN is distinguished from another--the binding data stored in the cell
// identifies the pointer of the FRAME! to exit).
//
// Actions have an associated Array of data, accessible via ACT_DETAILS().
// This is where they can store information that will be available when the
// dispatcher is called.
//
REBACT *Make_Action(
    Array* paramlist,
    REBNAT dispatcher, // native C function called by Eval_Core
    REBACT *opt_underlying, // optional underlying function
    VarList* opt_exemplar, // if provided, should be consistent w/next level
    REBLEN details_capacity // desired capacity of the ACT_DETAILS() array
){
    Assert_Flex_Managed(paramlist);

    Cell* rootparam = Array_Head(paramlist);
    assert(VAL_TYPE_RAW(rootparam) == REB_ACTION); // !!! not fully formed...
    assert(rootparam->payload.action.paramlist == paramlist);
    assert(rootparam->extra.binding == UNBOUND); // archetype

    // Precalculate cached function flags.
    //
    // Note: CELL_FLAG_ACTION_DEFERS_LOOKBACK is only relevant for un-refined-calls.
    // No lookback function calls trigger from PATH!.  HOWEVER: specialization
    // does come into play because it may change what the first "real"
    // argument is.  But again, we're only interested in specialization's
    // removal of *non-refinement* arguments.

    bool first_arg = true;

    Value* param = KNOWN(rootparam) + 1;
    for (; NOT_END(param); ++param) {
        switch (VAL_PARAM_CLASS(param)) {
        case PARAM_CLASS_LOCAL:
            break; // skip

        case PARAM_CLASS_RETURN: {
            assert(Cell_Parameter_Id(param) == SYM_RETURN);

            // See notes on CELL_FLAG_ACTION_INVISIBLE.
            //
            if (VAL_TYPESET_BITS(param) == 0)
                Set_Cell_Flag(rootparam, ACTION_INVISIBLE);
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
                Set_Cell_Flag(rootparam, ACTION_DEFERS_LOOKBACK);
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
                Set_Cell_Flag(rootparam, ACTION_QUOTES_FIRST_ARG);
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

    Array* details = Make_Array_Core(details_capacity, NODE_FLAG_MANAGED);
    Term_Array_Len(details, details_capacity);

    rootparam->payload.action.details = details;

    MISC(details).dispatcher = dispatcher; // level of indirection, hijackable

    assert(Is_Pointer_Corrupt_Debug(LINK(paramlist).corrupt));

    if (opt_underlying)
        LINK(paramlist).underlying = opt_underlying;
    else {
        // To avoid nullptr checking when a function is called and looking for
        // underlying, just use the action's own paramlist if needed.
        //
        LINK(paramlist).underlying = ACT(paramlist);
    }

    if (not opt_exemplar) {
        //
        // No exemplar is used as a cue to set the "specialty" to paramlist,
        // so that Push_Action() can assign L->special directly from it in
        // dispatch, and be equal to L->param.
        //
        LINK(details).specialty = paramlist;
    }
    else {
        // The parameters of the paramlist should line up with the slots of
        // the exemplar (though some of these parameters may be hidden due to
        // specialization, see REB_TS_HIDDEN).
        //
        assert(Is_Node_Managed(opt_exemplar));
        assert(Varlist_Len(opt_exemplar) == Array_Len(paramlist) - 1);

        LINK(details).specialty = Varlist_Array(opt_exemplar);
    }

    // The meta information may already be initialized, since the native
    // version of paramlist construction sets up the FUNCTION-META information
    // used by HELP.  If so, it must be a valid VarList*.  Otherwise nullptr.
    //
    assert(
        not MISC(paramlist).meta
        or Get_Array_Flag(MISC(paramlist).meta, IS_VARLIST)
    );

    assert(Not_Array_Flag(paramlist, HAS_FILE_LINE));
    assert(Not_Array_Flag(details, HAS_FILE_LINE));

    return ACT(paramlist);
}


//
//  Make_Expired_Level_Ctx_Managed: C
//
// FUNC/PROC bodies contain relative words and relative arrays.  Arrays from
// this relativized body may only be put into a specified Value once they
// have been combined with a frame.
//
// Reflection asks for action body data, when no instance is called.  Hence
// a Value must be produced somehow.  If the body is being copied, then the
// option exists to convert all the references to unbound...but this isn't
// representative of the actual connections in the body.
//
// There could be an additional "archetype" state for the relative binding
// machinery.  But making a one-off expired frame is an inexpensive option.
//
VarList* Make_Expired_Level_Ctx_Managed(REBACT *a)
{
    // Since passing SERIES_MASK_CONTEXT includes FLEX_FLAG_ALWAYS_DYNAMIC,
    // don't pass it in to the allocation...it needs to be set, but will be
    // overridden by FLEX_INFO_INACCESSIBLE.
    //
    Array* varlist = Alloc_Singular(
        ARRAY_FLAG_IS_VARLIST
        | NODE_FLAG_MANAGED
    );
    Set_Flex_Flag(varlist, ALWAYS_DYNAMIC);  // asserts check
    Set_Flex_Info(varlist, INACCESSIBLE);
    MISC(varlist).meta = nullptr;

    Cell* rootvar = RESET_CELL(ARR_SINGLE(varlist), REB_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = a;
    INIT_BINDING(rootvar, UNBOUND); // !!! is a binding relevant?

    VarList* expired = CTX(varlist);
    Tweak_Keylist_Of_Varlist_Shared(expired, ACT_PARAMLIST(a));

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
void Get_Maybe_Fake_Action_Body(Value* out, const Value* action)
{
    Specifier* binding = VAL_BINDING(action);
    REBACT *a = VAL_ACTION(action);

    // A Hijacker *might* not need to splice itself in with a dispatcher.
    // But if it does, bypass it to get to the "real" action implementation.
    //
    // !!! Should the source inject messages like {This is a hijacking} at
    // the top of the returned body?
    //
    while (ACT_DISPATCHER(a) == &Hijacker_Dispatcher) {
        a = VAL_ACTION(Array_Head(ACT_DETAILS(a)));
        // !!! Review what should happen to binding
    }

    Array* details = ACT_DETAILS(a);

    // !!! Should the binding make a difference in the returned body?  It is
    // exposed programmatically via CONTEXT OF.
    //
    UNUSED(binding);

    if (
        ACT_DISPATCHER(a) == &Null_Dispatcher
        or ACT_DISPATCHER(a) == &Nothing_Dispatcher
        or ACT_DISPATCHER(a) == &Unchecked_Dispatcher
        or ACT_DISPATCHER(a) == &Eraser_Dispatcher
        or ACT_DISPATCHER(a) == &Returner_Dispatcher
        or ACT_DISPATCHER(a) == &Block_Dispatcher
    ){
        // Interpreted code, the body is a block with some bindings relative
        // to the action.

        Cell* body = Array_Head(details);

        // The CELL_FLAG_ACTION_LEAVE/CELL_FLAG_ACTION_RETURN tricks for definitional
        // scoping make it seem like a generator authored more code in the
        // action's body...but the code isn't *actually* there and an
        // optimized internal trick is used.  Fake the code if needed.

        Value* example;
        REBLEN real_body_index;
        if (ACT_DISPATCHER(a) == &Eraser_Dispatcher) {
            example = Get_System(SYS_STANDARD, STD_PROC_BODY);
            real_body_index = 4;
        }
        else if (GET_ACT_FLAG(a, ACTION_RETURN)) {
            example = Get_System(SYS_STANDARD, STD_FUNC_BODY);
            real_body_index = 4;
        }
        else {
            example = nullptr;
            real_body_index = 0; // avoid compiler warning
            UNUSED(real_body_index);
        }

        Array* real_body = Cell_Array(body);
        assert(Get_Flex_Info(real_body, FROZEN_DEEP));

        Array* maybe_fake_body;
        if (example == nullptr) {
            maybe_fake_body = real_body;
            assert(Get_Flex_Info(maybe_fake_body, FROZEN_DEEP));
        }
        else {
            // See %sysobj.r for STANDARD/FUNC-BODY and STANDARD/PROC-BODY
            //
            maybe_fake_body = Copy_Array_Shallow_Flags(
                Cell_Array(example),
                VAL_SPECIFIER(example),
                NODE_FLAG_MANAGED
            );
            Set_Flex_Info(maybe_fake_body, FROZEN_DEEP);

            // Index 5 (or 4 in zero-based C) should be #BODY, a "real" body.
            // To give it the appearance of executing code in place, we use
            // a GROUP!.

            Cell* slot = Array_At(maybe_fake_body, real_body_index); // #BODY
            assert(Is_Issue(slot));

            RESET_VAL_HEADER_EXTRA(slot, REB_GROUP, 0); // clear VAL_FLAG_LINE
            INIT_VAL_ARRAY(slot, Cell_Array(body));
            VAL_INDEX(slot) = 0;
            INIT_BINDING(slot, a); // relative binding
        }

        // Cannot give user a relative value back, so make the relative
        // body specific to a fabricated expired frame.  See #2221

        RESET_VAL_HEADER_EXTRA(out, REB_BLOCK, 0);
        INIT_VAL_ARRAY(out, maybe_fake_body);
        VAL_INDEX(out) = 0;
        INIT_BINDING(out, Make_Expired_Level_Ctx_Managed(a));
        return;
    }

    if (ACT_DISPATCHER(a) == &Specializer_Dispatcher) {
        //
        // The FRAME! stored in the body for the specialization has a phase
        // which is actually the function to be run.
        //
        Value* frame = KNOWN(Array_Head(details));
        assert(Is_Frame(frame));
        Copy_Cell(out, frame);
        return;
    }

    if (ACT_DISPATCHER(a) == &Generic_Dispatcher) {
        Value* verb = KNOWN(Array_Head(details));
        assert(Is_Word(verb));
        Copy_Cell(out, verb);
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
//         [{Returns a value from a function.} value [~null~ any-value!]]
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
    const Value* spec,
    const Value* code,
    Flags mkf_flags // MKF_RETURN, etc.
) {
    assert(Is_Block(spec) and Is_Block(code));

    REBACT *a = Make_Action(
        Make_Paramlist_Managed_May_Fail(spec, mkf_flags),
        &Null_Dispatcher, // will be overwritten if non-[] body
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        1 // details array capacity
    );

    // We look at the *actual* function flags; e.g. the person may have used
    // the FUNC generator (with MKF_RETURN) but then named a parameter RETURN
    // which overrides it, so the value won't have CELL_FLAG_ACTION_RETURN.
    //
    Value* value = ACT_ARCHETYPE(a);

    Array* copy;
    if (VAL_ARRAY_LEN_AT(code) == 0) { // optimize empty body case

        if (Get_Cell_Flag(value, ACTION_INVISIBLE)) {
            ACT_DISPATCHER(a) = &Commenter_Dispatcher;
        }
        else if (Get_Cell_Flag(value, ACTION_TRASHER)) {
            ACT_DISPATCHER(a) = &Eraser_Dispatcher;
        }
        else if (Get_Cell_Flag(value, ACTION_RETURN)) {
            Value* typeset = ACT_PARAM(a, ACT_NUM_PARAMS(a));
            assert(Cell_Parameter_Id(typeset) == SYM_RETURN);
            if (not TYPE_CHECK(typeset, REB_MAX_NULLED)) // what eval [] returns
                ACT_DISPATCHER(a) = &Returner_Dispatcher; // error when run
        }
        else {
            // Keep the Null_Dispatcher passed in above
        }

        // Reusing EMPTY_ARRAY won't allow adding ARRAY_FLAG_HAS_FILE_LINE bits
        //
        copy = Make_Array_Core(1, NODE_FLAG_MANAGED);
    }
    else { // body not empty, pick dispatcher based on output disposition

        if (Get_Cell_Flag(value, ACTION_INVISIBLE))
            ACT_DISPATCHER(a) = &Elider_Dispatcher; // no L->out mutation
        else if (Get_Cell_Flag(value, ACTION_TRASHER))
            ACT_DISPATCHER(a) = &Eraser_Dispatcher; // forces L->out trash
        else if (Get_Cell_Flag(value, ACTION_RETURN))
            ACT_DISPATCHER(a) = &Returner_Dispatcher; // type checks L->out
        else
            ACT_DISPATCHER(a) = &Unchecked_Dispatcher; // unchecked L->out

        copy = Copy_And_Bind_Relative_Deep_Managed(
            code, // new copy has locals bound relatively to the new action
            ACT_PARAMLIST(a),
            TS_WORD
        );
    }

    Cell* body = RESET_CELL(Array_Head(ACT_DETAILS(a)), REB_BLOCK);
    INIT_VAL_ARRAY(body, copy);
    VAL_INDEX(body) = 0;
    INIT_BINDING(body, a); // Record that block is relative to a function

    // Favor the spec first, then the body, for file and line information.
    //
    if (Get_Array_Flag(Cell_Array(spec), HAS_FILE_LINE)) {
        LINK(copy).file = LINK(Cell_Array(spec)).file;
        MISC(copy).line = MISC(Cell_Array(spec)).line;
        Set_Array_Flag(copy, HAS_FILE_LINE);
    }
    else if (Get_Array_Flag(Cell_Array(code), HAS_FILE_LINE)) {
        LINK(copy).file = LINK(Cell_Array(code)).file;
        MISC(copy).line = MISC(Cell_Array(code)).line;
        Set_Array_Flag(copy, HAS_FILE_LINE);
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
    Deep_Freeze_Array(Cell_Array(body));
  #else
    if (not LEGACY(OPTIONS_UNLOCKED_SOURCE))
        Deep_Freeze_Array(Cell_Array(body));
  #endif

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
    UNUSED(level_);
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
//
Bounce Generic_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));

    enum Reb_Kind kind = VAL_TYPE(Level_Arg(L, 1));
    Value* verb = KNOWN(Array_Head(details));
    assert(Is_Word(verb));
    assert(kind < REB_MAX);

    GENERIC_HOOK hook = Generic_Hooks[kind];
    return hook(L, verb);
}


//
//  Null_Dispatcher: C
//
// If you write `func [...] []` it uses this dispatcher instead of running
// Eval_Core_Throws() on an empty block.  This serves more of a point than
// it sounds, because you can make fast stub actions that only cost if they
// are HIJACK'd (e.g. ASSERT is done this way).
//
Bounce Null_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(LVL_PHASE_OR_DUMMY(L));
    assert(Cell_Series_Len_At(Array_Head(details)) == 0);
    UNUSED(details);

    return nullptr;
}


//
//  Nothing_Dispatcher: C
//
// Analogue to Null_Dispatcher() for `func [return: [~] ...] []`.
//
Bounce Nothing_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    assert(Cell_Series_Len_At(Array_Head(details)) == 0);
    UNUSED(details);

    return Init_Nothing(L->out);
}


//
//  Datatype_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a datatype.
//
Bounce Datatype_Checker_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* datatype = Array_Head(details);
    assert(Is_Datatype(datatype));

    return Init_Logic(
        L->out,
        VAL_TYPE(Level_Arg(L, 1)) == VAL_TYPE_KIND(datatype)
    );
}


//
//  Typeset_Checker_Dispatcher: C
//
// Dispatcher used by TYPECHECKER generator for when argument is a typeset.
//
Bounce Typeset_Checker_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* typeset = Array_Head(details);
    assert(Is_Typeset(typeset));

    return Init_Logic(L->out, TYPE_CHECK(typeset, VAL_TYPE(Level_Arg(L, 1))));
}


//
//  Unchecked_Dispatcher: C
//
// This is the default MAKE ACTION! dispatcher for interpreted functions
// (whose body is a block that runs through DO []).  There is no return type
// checking done on these simple functions.
//
Bounce Unchecked_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* body = Array_Head(details);
    assert(Is_Block(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    if (Eval_Array_At_Throws(L->out, Cell_Array(body), 0, SPC(L->varlist)))
        return BOUNCE_THROWN;

    return L->out;
}


//
//  Eraser_Dispatcher: C
//
// Variant of Unchecked_Dispatcher, except sets the output value to trash.
// Pushing that code into the dispatcher means there's no need to do flag
// testing in the main loop.
//
Bounce Eraser_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* body = Array_Head(details);
    assert(Is_Block(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    if (Eval_Array_At_Throws(L->out, Cell_Array(body), 0, SPC(L->varlist)))
        return BOUNCE_THROWN;

    return Init_Nothing(L->out);
}


//
//  Returner_Dispatcher: C
//
// Contrasts with the Unchecked_Dispatcher since it ensures the return type is
// correct.  (Note that natives do not get this type checking, and they
// probably shouldn't pay for it except in the debug build.)
//
Bounce Returner_Dispatcher(Level* L)
{
    REBACT *phase = Level_Phase(L);
    Array* details = ACT_DETAILS(phase);

    Cell* body = Array_Head(details);
    assert(Is_Block(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    if (Eval_Array_At_Throws(L->out, Cell_Array(body), 0, SPC(L->varlist)))
        return BOUNCE_THROWN;

    Value* typeset = ACT_PARAM(phase, ACT_NUM_PARAMS(phase));
    assert(Cell_Parameter_Id(typeset) == SYM_RETURN);

    // Typeset bits for locals in frames are usually ignored, but the RETURN:
    // local uses them for the return types of a "virtual" definitional return
    // if the parameter is PARAM_CLASS_RETURN_1.
    //
    if (not TYPE_CHECK(typeset, VAL_TYPE(L->out)))
        fail (Error_Bad_Return_Type(L, VAL_TYPE(L->out)));

    return L->out;
}


//
//  Elider_Dispatcher: C
//
// This is used by "invisible" functions (who in their spec say `return: []`).
// The goal is to evaluate a function call in such a way that its presence
// doesn't disrupt the chain of evaluation any more than if the call were not
// there.  (The call can have side effects, however.)
//
Bounce Elider_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));

    Cell* body = Array_Head(details);
    assert(Is_Block(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    // !!! It would be nice to use the frame's spare "cell" for the thrownaway
    // result, but Fetch_Next code expects to use the cell.
    //
    DECLARE_VALUE (dummy);
    SET_END(dummy);

    if (Eval_Array_At_Throws(dummy, Cell_Array(body), 0, SPC(L->varlist))) {
        Copy_Cell(L->out, dummy); // can't return a local variable
        return BOUNCE_THROWN;
    }

    return BOUNCE_INVISIBLE;
}


//
//  Commenter_Dispatcher: C
//
// This is a specialized version of Elider_Dispatcher() for when the body of
// a function is empty.  This helps COMMENT and functions like it run faster.
//
Bounce Commenter_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* body = Array_Head(details);
    assert(Cell_Series_Len_At(body) == 0);
    UNUSED(body);
    return BOUNCE_INVISIBLE;
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
Bounce Hijacker_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Cell* hijacker = Array_Head(details);

    // We need to build a new frame compatible with the hijacker, and
    // transform the parameters we've gathered to be compatible with it.
    //
    if (Redo_Action_Throws(L, VAL_ACTION(hijacker)))
        return BOUNCE_THROWN;

    return L->out;
}


//
//  Adapter_Dispatcher: C
//
// Dispatcher used by ADAPT.
//
Bounce Adapter_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    assert(Array_Len(details) == 2);

    Cell* prelude = Array_At(details, 0);
    Value* adaptee = KNOWN(Array_At(details, 1));

    // The first thing to do is run the prelude code, which may throw.  If it
    // does throw--including a RETURN--that means the adapted function will
    // not be run.
    //
    // We can't do the prelude into L->out in the case that this is an
    // adaptation of an invisible (e.g. DUMP).  Would be nice to use the frame
    // spare cell but can't as Fetch_Next() uses it.

    DECLARE_VALUE (dummy);
    if (Eval_Array_At_Throws(
        dummy,
        Cell_Array(prelude),
        VAL_INDEX(prelude),
        SPC(L->varlist)
    )){
        Copy_Cell(L->out, dummy);
        return BOUNCE_THROWN;
    }

    Level_Phase(L) = VAL_ACTION(adaptee);
    LVL_BINDING(L) = VAL_BINDING(adaptee);

    return BOUNCE_REDO_CHECKED; // the redo will use the updated phase/binding
}


//
//  Encloser_Dispatcher: C
//
// Dispatcher used by ENCLOSE.
//
Bounce Encloser_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    assert(Array_Len(details) == 2);

    Value* inner = KNOWN(Array_At(details, 0)); // same args as f
    assert(Is_Action(inner));
    Value* outer = KNOWN(Array_At(details, 1)); // takes 1 arg (a FRAME!)
    assert(Is_Action(outer));

    // We want to call OUTER with a FRAME! value that will dispatch to INNER
    // when (and if) it runs DO on it.  That frame is the one built for this
    // call to the encloser.  If it isn't managed, there's no worries about
    // user handles on it...so just take it.  Otherwise, "steal" its vars.
    //
    VarList* c = Steal_Context_Vars(CTX(L->varlist), Level_Phase(L));
    LINK(c).keysource = VAL_ACTION(inner);

    assert(Get_Flex_Info(L->varlist, INACCESSIBLE)); // look dead

    // L->varlist may or may not have wound up being managed.  It was not
    // allocated through the usual mechanisms, so if unmanaged it's not in
    // the tracking list Init_Any_Context() expects.  Just fiddle the bit.
    //
    Set_Node_Managed_Bit(c);

    // When the DO of the FRAME! executes, we don't want it to run the
    // encloser again (infinite loop).
    //
    Value* rootvar = Varlist_Archetype(c);
    rootvar->payload.any_context.phase = VAL_ACTION(inner);
    INIT_BINDING_MAY_MANAGE(rootvar, VAL_BINDING(inner));

    Copy_Cell(Level_Spare(L), rootvar); // user may DO this, or not...

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
    Set_Node_Managed_Bit(L->varlist);

    const bool fully = true;
    if (Apply_Only_Throws(L->out, fully, outer, Level_Spare(L), rebEND))
        return BOUNCE_THROWN;

    return L->out;
}


//
//  Cascader_Dispatcher: C
//
// Dispatcher used by CASCADE.
//
Bounce Cascader_Dispatcher(Level* L)
{
    Array* details = ACT_DETAILS(Level_Phase(L));
    Array* pipeline = Cell_Array(Array_Head(details));

    // The post-processing pipeline has to be "pushed" so it is not forgotten.
    // Go in reverse order, so the function to apply last is at the bottom of
    // the stack.
    //
    Value* pipeline_at = KNOWN(Array_Last(pipeline));
    for (; pipeline_at != Array_Head(pipeline); --pipeline_at) {
        assert(Is_Action(pipeline_at));
        Copy_Cell(PUSH(), KNOWN(pipeline_at));
    }

    // Extract the first function, itself which might be a cascade.
    //
    Level_Phase(L) = VAL_ACTION(pipeline_at);
    LVL_BINDING(L) = VAL_BINDING(pipeline_at);

    return BOUNCE_REDO_UNCHECKED; // signatures should match
}


//
//  Get_If_Word_Or_Path_Throws: C
//
// Some routines like APPLIQUE and SPECIALIZE are willing to take a WORD! or
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
    Value* out,
    Symbol* *opt_name_out,
    const Cell* v,
    Specifier* specifier,
    bool push_refinements
) {
    if (Is_Word(v)) {
        *opt_name_out = Cell_Word_Symbol(v);
        Move_Opt_Var_May_Fail(out, v, specifier);
    }
    else if (Is_Path(v)) {
        Specifier* derived = Derive_Specifier(specifier, v);
        if (Eval_Path_Throws_Core(
            out,
            opt_name_out, // requesting says we run functions (not GET-PATH!)
            Cell_Array(v),
            VAL_INDEX(v),
            derived,
            nullptr,  // `setval`: null means don't treat as SET-PATH!
            push_refinements
                ? DO_FLAG_PUSH_PATH_REFINEMENTS // pushed in reverse order
                : DO_MASK_NONE
        )){
            return true;
        }
    }
    else {
        *opt_name_out = nullptr;
        Derelativize(out, v, specifier);
    }

    return false;
}
