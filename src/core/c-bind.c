//
//  File: %c-bind.c
//  Summary: "Word Binding Routines"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// Binding relates a word to a context.  Every word can be either bound,
// specifically bound to a particular context, or bound relatively to a
// function (where additional information is needed in order to find the
// specific instance of the variable for that word as a key).
//

#include "sys-core.h"


//
//  Bind_Values_Inner_Loop: C
//
// Bind_Values_Core() sets up the binding table and then calls
// this recursive routine to do the actual binding.
//
void Bind_Values_Inner_Loop(
    struct Reb_Binder *binder,
    Cell* head,
    REBCTX *context,
    REBU64 bind_types, // !!! REVIEW: force word types low enough for 32-bit?
    REBU64 add_midstream_types,
    REBFLGS flags
) {
    Cell* v = head;
    for (; NOT_END(v); ++v) {
        REBU64 type_bit = FLAGIT_KIND(VAL_TYPE(v));

        if (type_bit & bind_types) {
            Symbol* canon = VAL_WORD_CANON(v);
            REBINT n = Get_Binder_Index_Else_0(binder, canon);
            if (n > 0) {
                //
                // A binder index of 0 should clearly not be bound.  But
                // negative binder indices are also ignored by this process,
                // which provides a feature of building up state about some
                // words while still not including them in the bind.
                //
                assert(cast(REBLEN, n) <= CTX_LEN(context));

                // We're overwriting any previous binding, which may have
                // been relative.

                INIT_BINDING_MAY_MANAGE(v, context);
                INIT_WORD_INDEX(v, n);
            }
            else if (type_bit & add_midstream_types) {
                //
                // Word is not in context, so add it if option is specified
                //
                Expand_Context(context, 1);
                Append_Context(context, v, nullptr);
                Add_Binder_Index(binder, canon, VAL_WORD_INDEX(v));
            }
        }
        else if (Any_List(v) and (flags & BIND_DEEP)) {
            Bind_Values_Inner_Loop(
                binder,
                Cell_List_At(v),
                context,
                bind_types,
                add_midstream_types,
                flags
            );
        }
    }
}


//
//  Bind_Values_Core: C
//
// Bind words in an array of values terminated with END
// to a specified context.  See warnings on the functions like
// Bind_Values_Deep() about not passing just a singular cell.
//
// NOTE: If types are added, then they will be added in "midstream".  Only
// bindings that come after the added value is seen will be bound.
//
void Bind_Values_Core(
    Cell* head,
    REBCTX *context,
    REBU64 bind_types,
    REBU64 add_midstream_types,
    REBFLGS flags // see %sys-core.h for BIND_DEEP, etc.
) {
    struct Reb_Binder binder;
    INIT_BINDER(&binder, context);

    // Associate the canon of a word with an index number.  (This association
    // is done by poking the index into the Stub of the series behind the
    // ANY-WORD!, so it must be cleaned up to not break future bindings.)

    if (context == Lib_Context) {
        // bind indices cached in canon symbol bind_index.lib
    }
    else {
        REBLEN index = 1;
        Value* key = CTX_KEYS_HEAD(context);
        for (; index <= CTX_LEN(context); key++, index++)
            if (not Is_Param_Unbindable(key))
                Add_Binder_Index(&binder, Key_Canon(key), index);
    }

    Bind_Values_Inner_Loop(
        &binder, head, context, bind_types, add_midstream_types, flags
    );

    // Reset all the binder indices to zero, balancing out what was added.

    if (context == Lib_Context) {
        // leave bind indices cached in canon symbol bind_index.lib
    }
    else {
        Value* key = CTX_KEYS_HEAD(context);
        for (; NOT_END(key); key++)
            if (not Is_Param_Unbindable(key))
                Remove_Binder_Index(&binder, Key_Canon(key));
    }

    SHUTDOWN_BINDER(&binder);
}


//
//  Unbind_Values_Core: C
//
// Unbind words in a block, optionally unbinding those which are
// bound to a particular target (if target is NULL, then all
// words will be unbound regardless of their VAL_WORD_CONTEXT).
//
void Unbind_Values_Core(Cell* head, REBCTX *context, bool deep)
{
    Cell* v = head;
    for (; NOT_END(v); ++v) {
        if (
            Any_Word(v)
            and (not context or VAL_BINDING(v) == context)
        ){
            Unbind_Any_Word(v);
        }
        else if (Any_List(v) and deep)
            Unbind_Values_Core(Cell_List_At(v), context, true);
    }
}


//
//  Try_Bind_Word: C
//
// Returns 0 if word is not part of the context, otherwise the index of the
// word in the context.
//
REBLEN Try_Bind_Word(REBCTX *context, Value* word)
{
    REBLEN n = Find_Canon_In_Context(context, VAL_WORD_CANON(word), false);
    if (n != 0) {
        INIT_BINDING(word, context); // binding may have been relative before
        INIT_WORD_INDEX(word, n);
    }
    return n;
}


//
//  Bind_Relative_Inner_Loop: C
//
// Recursive function for relative function word binding.
//
// !!! Should this return true if any relative bindings were made?
//
static void Bind_Relative_Inner_Loop(
    struct Reb_Binder *binder,
    Cell* head,
    Array* paramlist,
    REBU64 bind_types
) {
    Cell* v = head;

    for (; NOT_END(v); ++v) {
        REBU64 type_bit = FLAGIT_KIND(VAL_TYPE(v));

        // The two-pass copy-and-then-bind should have gotten rid of all the
        // relative values to other functions during the copy.
        //
        // !!! Long term, in a single pass copy, this would have to deal
        // with relative values and run them through the specification
        // process if they were not just getting overwritten.
        //
        assert(not IS_RELATIVE(v));

        if (type_bit & bind_types) {
            REBINT n = Get_Binder_Index_Else_0(binder, VAL_WORD_CANON(v));
            if (n != 0) {
                //
                // Word's canon symbol is in frame.  Relatively bind it.
                // (clear out existing binding flags first).
                //
                Unbind_Any_Word(v);
                INIT_BINDING(v, paramlist); // incomplete func
                INIT_WORD_INDEX(v, n);
            }
        }
        else if (Any_List(v)) {
            Bind_Relative_Inner_Loop(
                binder, Cell_List_At(v), paramlist, bind_types
            );

            // !!! Technically speaking it is not necessary for an array to
            // be marked relative if it doesn't contain any relative words
            // under it.  However, for uniformity in the near term, it's
            // easiest to debug if there is a clear mark on arrays that are
            // part of a deep copy of a function body either way.
            //
            INIT_BINDING(v, paramlist); // incomplete func
        }
    }
}


//
//  Copy_And_Bind_Relative_Deep_Managed: C
//
// This routine is called by Make_Action in order to take the raw material
// given as a function body, and de-relativize any IS_RELATIVE(value)s that
// happen to be in it already (as any Copy does).  But it also needs to make
// new relative references to ANY-WORD! that are referencing function
// parameters, as well as to relativize the copies of ANY-ARRAY! that contain
// these relative words...so that they refer to the archetypal function
// to which they should be relative.
//
Array* Copy_And_Bind_Relative_Deep_Managed(
    const Value* body,
    Array* paramlist, // body of function is not actually ready yet
    REBU64 bind_types
) {
    // !!! Currently this is done in two phases, because the historical code
    // would use the generic copying code and then do a bind phase afterward.
    // Both phases are folded into this routine to make it easier to make
    // a one-pass version when time permits.
    //
    Array* copy = Copy_Array_Core_Managed(
        Cell_Array(body),
        VAL_INDEX(body), // at
        VAL_SPECIFIER(body),
        Cell_Series_Len_At(body), // tail
        0, // extra
        ARRAY_FLAG_HAS_FILE_LINE, // ask to preserve file and line info
        TS_SERIES & ~TS_NOT_COPIED // types to copy deeply
    );

    struct Reb_Binder binder;
    INIT_BINDER(&binder, nullptr);

    // Setup binding table from the argument word list
    //
    REBLEN index = 1;
    Cell* param = Array_At(paramlist, 1); // [0] is ACTION! value
    for (; NOT_END(param); param++, index++)
        Add_Binder_Index(&binder, Key_Canon(param), index);

    Bind_Relative_Inner_Loop(&binder, Array_Head(copy), paramlist, bind_types);

    // Reset binding table
    //
    param = Array_At(paramlist, 1); // [0] is ACTION! value
    for (; NOT_END(param); param++)
        Remove_Binder_Index(&binder, Key_Canon(param));

    SHUTDOWN_BINDER(&binder);
    return copy;
}


//
//  Rebind_Values_Deep: C
//
// Rebind all words that reference src target to dst target.
// Rebind is always deep.
//
void Rebind_Values_Deep(
    REBCTX *src,
    REBCTX *dst,
    Cell* head,
    struct Reb_Binder *opt_binder
) {
    Cell* v = head;
    for (; NOT_END(v); ++v) {
        if (Any_List(v)) {
            Rebind_Values_Deep(src, dst, Cell_List_At(v), opt_binder);
        }
        else if (Any_Word(v) and VAL_BINDING(v) == src) {
            INIT_BINDING(v, dst);

            if (opt_binder != nullptr) {
                INIT_WORD_INDEX(
                    v,
                    Get_Binder_Index_Else_0(opt_binder, VAL_WORD_CANON(v))
                );
            }
        }
        else if (Is_Action(v)) {
            //
            // !!! This is a new take on R3-Alpha's questionable feature of
            // deep copying function bodies and rebinding them when a
            // derived object was made.  Instead, if a function is bound to
            // a "base class" of the object we are making, that function's
            // binding pointer (in the function's value cell) is changed to
            // be this object.
            //
            Specifier* binding = VAL_BINDING(v);
            if (binding == UNBOUND) {
                //
                // Leave non bindings alone.  Hence, unlike in R3-Alpha, an
                // ordinary FUNC won't forward its references.  An explicit
                // BIND to an object must be performed, or METHOD should be
                // used to do it implicitly.
            }
            else {
                REBCTX *stored = CTX(binding);
                if (Is_Overriding_Context(stored, dst))
                    INIT_BINDING(v, dst);
                else {
                    // Could be bound to a reified frame context, or just
                    // to some other object not related to this derivation.
                }
            }
        }
    }
}


//
//  Virtual_Bind_Deep_To_New_Context: C
//
// Looping constructs which are parameterized by WORD!s to set each time
// through the loop must copy the body in R3-Alpha's model.  For instance:
//
//    for-each [x y] [1 2 3] [print ["this body must be copied for" x y]]
//
// The reason is because the context in which X and Y live does not exist
// prior to the execution of the FOR-EACH.  And if the body were destructively
// rebound, then this could mutate and disrupt bindings of code that was
// intended to be reused.
//
// (Note that R3-Alpha was somewhat inconsistent on the idea of being
// sensitive about non-destructively binding arguments in this way.
// MAKE OBJECT! purposefully mutated bindings in the passed-in block.)
//
// The context is effectively an ordinary object, and outlives the loop:
//
//     x-word: none
//     for-each x [1 2 3] [x-word: 'x | break]
//     get x-word ;-- returns 3
//
// Ren-C adds a feature of letting ISSUE!s be used to indicate that the
// loop variable should be written into the existing bound variable that the
// ISSUE! specified.  If all loop variables are of this form, then no
// copy will be made.
//
// !!! Ren-C managed to avoid deep copying function bodies yet still get
// "specific binding" by means of "relative values" (RELVALs) and specifiers.
// Extending this approach is hoped to be able to avoid the deep copy, and
// the speculative name of "virtual binding" is given to this routine...even
// though it is actually copying.
//
// !!! With stack-backed contexts in Ren-C, it may be the case that the
// chunk stack is used as backing memory for the loop, so it can be freed
// when the loop is over and word lookups will error.
//
// !!! Since a copy is made at time of writing (as opposed to using a binding
// "view" of the same underlying data), the locked status of series is not
// mirrored.  A short term remedy might be to parameterize copying such that
// it mirrors the locks, but longer term remedy will hopefully be better.
//
void Virtual_Bind_Deep_To_New_Context(
    Value* body_in_out, // input *and* output parameter
    REBCTX **context_out,
    const Value* spec
) {
    assert(Is_Block(body_in_out));

    REBLEN num_vars = Is_Block(spec) ? Cell_Series_Len_At(spec) : 1;
    if (num_vars == 0)
        fail (Error_Invalid(spec));

    const Cell* item;
    Specifier* specifier;
    bool rebinding;
    if (Is_Block(spec)) {
        item = Cell_List_At(spec);
        specifier = VAL_SPECIFIER(spec);

        rebinding = false;
        for (; NOT_END(item); ++item) {
            if (Is_Word(item) or Is_Refinement(item) or Is_Lit_Word(item))
                rebinding = true;
            else if (not Is_Issue(item)) {
                //
                // Better to fail here, because if we wait until we're in
                // the middle of building the context, the managed portion
                // (keylist) would be incomplete and tripped on by the GC if
                // we didn't do some kind of workaround.
                //
                fail (Error_Invalid_Core(item, specifier));
            }
        }

        item = Cell_List_At(spec);
    }
    else {
        item = spec;
        specifier = SPECIFIED;
        if (Is_Word(item) or Is_Refinement(item) or Is_Lit_Word(item))
            rebinding = true;
        else if (Is_Issue(item))
            rebinding = false;
        else
            fail (Error_Invalid_Core(item, specifier));
    }

    // If we need to copy the body, do that *first*, because copying can
    // fail() (out of memory, or cyclical recursions, etc.) and that can't
    // happen while a binder is in effect unless we PUSH_TRAP to catch and
    // correct for it, which has associated cost.
    //
    if (rebinding) {
        //
        // Note that this deep copy of the block isn't exactly semantically
        // the same, because it's truncated before the index.  You cannot
        // go BACK on it before the index.
        //
        Init_Block(
            body_in_out,
            Copy_Array_Core_Managed(
                Cell_Array(body_in_out),
                VAL_INDEX(body_in_out), // at
                VAL_SPECIFIER(body_in_out),
                Array_Len(Cell_Array(body_in_out)), // tail
                0, // extra
                ARRAY_FLAG_HAS_FILE_LINE, // flags
                TS_LIST // types to copy deeply
            )
        );
    }
    else {
        // Just leave body_in_out as it is, and make the context
    }

    // Keylists are always managed, but varlist is unmanaged by default (so
    // it can be freed if there is a problem)
    //
    *context_out = Alloc_Context(REB_OBJECT, num_vars);

    REBCTX *c = *context_out; // for convenience...

    // We want to check for duplicates and a Binder can be used for that
    // purpose--but note that a fail() cannot happen while binders are
    // in effect UNLESS the BUF_COLLECT contains information to undo it!
    // There's no BUF_COLLECT here, so don't fail while binder in effect.
    //
    struct Reb_Binder binder;
    if (rebinding)
        INIT_BINDER(&binder, *context_out);

    Symbol* duplicate = nullptr;

    Value* key = CTX_KEYS_HEAD(c);
    Value* var = CTX_VARS_HEAD(c);

    REBLEN index = 1;
    while (index <= num_vars) {
        if (Is_Word(item) or Is_Refinement(item) or Is_Lit_Word(item)) {
            Init_Typeset(
                key,
                TS_VALUE, // !!! Currently not paid attention to
                Cell_Word_Symbol(item)
            );

            // !!! For loops, nothing should be able to be aware of this
            // synthesized variable until the loop code has initialized it
            // with something.  However, in case any other code gets run,
            // it can't be left trash...so we'd need it to be at least an
            // unreadable blank.  But since this code is also shared with USE,
            // it doesn't do any initialization...so go ahead and put trash.
            //
            Init_Nothing(var);

            assert(rebinding); // shouldn't get here unless we're rebinding

            if (not Try_Add_Binder_Index(
                &binder, Cell_Param_Canon(key), index
            )){
                // We just remember the first duplicate, but we go ahead
                // and fill in all the keylist slots to make a valid array
                // even though we plan on failing.  Duplicates count as a
                // problem even if they are LIT-WORD! (negative index) as
                // `for-each [x 'x] ...` is paradoxical.
                //
                if (duplicate == nullptr)
                    duplicate = Cell_Parameter_Symbol(key);
            }
        }
        else {
            assert(Is_Issue(item)); // checked previously

            // An ISSUE! indicates that we wish to use the original binding.
            // So `for-each #x [1 2 3] [...]` will actually set that x
            // instead of creating a new one.  (New executables use @x)
            //
            // !!! Enumerations in the code walks through the context varlist,
            // setting the loop variables as they go.  It doesn't walk through
            // the array the user gave us, so if it's a GET-WORD! the
            // information is lost.  Do a trick where we put the GET-WORD!
            // itself into the slot, and give it NODE_FLAG_MARKED...then
            // hide it from the context and binding.
            //
            Init_Typeset(
                key,
                TS_VALUE, // !!! Currently not paid attention to
                Cell_Word_Symbol(item)
            );
            TYPE_SET(key, REB_TS_UNBINDABLE);
            TYPE_SET(key, REB_TS_HIDDEN);
            Derelativize(var, item, specifier);
            SET_VAL_FLAG(var, CELL_FLAG_PROTECTED);
            SET_VAL_FLAG(var, VAR_MARKED_REUSE);

            // We don't want to stop `for-each [:x :x] ...` necessarily,
            // because if we're saying we're using the existing binding they
            // could be bound to different things.  But if they're not bound
            // to different things, the last one in the list gets the final
            // assignment.  This would be harder to check against, but at
            // least allowing it doesn't make new objects with duplicate keys.
            // For now, don't bother trying to use a binder or otherwise to
            // stop it.
            //
            // However, `for-each [x :x] ...` is intrinsically contradictory.
            // So we use negative indices in the binder, which the binding
            // process will ignore.
            //
            if (rebinding) {
                REBINT stored = Get_Binder_Index_Else_0(
                    &binder, Cell_Param_Canon(key)
                );
                if (stored > 0) {
                    if (duplicate == nullptr)
                        duplicate = Cell_Parameter_Symbol(key);
                }
                else if (stored == 0) {
                    Add_Binder_Index(&binder, Cell_Param_Canon(key), -1);
                }
                else {
                    assert(stored == -1);
                }
            }
        }

        key++;
        var++;

        ++item;
        ++index;
    }

    Term_Array_Len(CTX_VARLIST(c), num_vars + 1);
    Term_Array_Len(CTX_KEYLIST(c), num_vars + 1);

    // As currently written, the loop constructs which use these contexts
    // will hold pointers into the arrays across arbitrary user code running.
    // If the context were allowed to expand, then this can cause memory
    // corruption:
    //
    // https://github.com/rebol/rebol-issues/issues/2274
    //
    Set_Flex_Flag(CTX_VARLIST(c), DONT_RELOCATE);

    // !!! In virtual binding, there would not be a Bind_Values call below;
    // so it wouldn't necessarily be required to manage the augmented
    // information.  For now it's a requirement for any references that
    // might be found...and INIT_BINDING_MAY_MANAGE() won't auto-manage
    // things unless they are stack-based.  Virtual bindings will be, but
    // contexts like this won't.
    //
    Manage_Flex(CTX_VARLIST(c));

    if (not rebinding)
        return; // nothing else needed to do

    if (not duplicate) {
        //
        // This is effectively `Bind_Values_Deep(Array_Head(body_out), context)`
        // but we want to reuse the binder we had anyway for detecting the
        // duplicates.
        //
        Bind_Values_Inner_Loop(
            &binder, Cell_List_At(body_in_out), c, TS_WORD, 0, BIND_DEEP
        );
    }

    // Must remove binder indexes for all words, even if about to fail
    //
    key = CTX_KEYS_HEAD(c);
    var = CTX_VARS_HEAD(c); // only needed for debug, optimized out
    for (; NOT_END(key); ++key, ++var) {
        REBINT stored = Remove_Binder_Index_Else_0(
            &binder, Cell_Param_Canon(key)
        );
        if (stored == 0)
            assert(duplicate);
        else if (stored > 0)
            assert(NOT_VAL_FLAG(var, NODE_FLAG_MARKED));
        else
            assert(GET_VAL_FLAG(var, NODE_FLAG_MARKED));
    }

    SHUTDOWN_BINDER(&binder);

    if (duplicate) {
        DECLARE_VALUE (word);
        Init_Word(word, duplicate);
        fail (Error_Dup_Vars_Raw(word));
    }
}


//
//  Init_Interning_Binder: C
//
// The global "binding table" is actually now pieces of data that live on the
// series nodes that store UTF-8 data for words.  This creates a mapping from
// canon word spellings to signed integers.
//
void Init_Interning_Binder(
    struct Reb_Binder *binder,
    REBCTX *ctx // location to bind into (in addition to lib)
){
    INIT_BINDER(binder, ctx);
    if (ctx == Lib_Context) {
        // indices already cached in bind_index.lib
    }
    else {
        Value* key = CTX_KEYS_HEAD(ctx);
        REBINT index = 1;
        for (; NOT_END(key); ++key, ++index)
            Add_Binder_Index(binder, Key_Canon(key), index);
    }
}


//
//  Shutdown_Interning_Binder: C
//
// This will remove the bindings added in Init_Interning_Binder, along with
// any other bindings which were incorporated along the way.
//
void Shutdown_Interning_Binder(struct Reb_Binder *binder, REBCTX *ctx)
{
    if (ctx == Lib_Context) {
        // indices cached in bind_index.lib, don't need removing
    }
    else {
        Value* key = CTX_KEYS_HEAD(ctx);
        REBINT index = 1;
        for (; NOT_END(key); ++key, ++index) {
            REBINT n = Remove_Binder_Index_Else_0(binder, Key_Canon(key));
            assert(n == index);
            UNUSED(n);
        }
    }

    SHUTDOWN_BINDER(binder);
}
