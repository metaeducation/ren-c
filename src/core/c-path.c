//
//  File: %c-path.h
//  Summary: "Core Path Dispatching and Chaining"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// When a path like `a/(b + c)/d` is evaluated, it moves in steps.  The
// evaluative result of chaining the prior steps is offered as input to
// the next step.  The path evaluator `Do_Path_Throws` delegates steps to
// type-specific "(P)ath (D)ispatchers" with names like PD_Context,
// PD_Array, etc.
//
// R3-Alpha left several open questions about the handling of paths.  One
// of the trickiest regards the mechanics of how to use a SET-PATH! to
// write data into native structures when more than one path step is
// required.  For instance:
//
//     >> gob/size
//     == 10x20
//
//     >> gob/size/x: 304
//     >> gob/size
//     == 10x304
//
// Because GOB! stores its size as packed bits that are not a full PAIR!,
// the `gob/size` path dispatch can't give back a pointer to a REBVAL* to
// which later writes will update the GOB!.  It can only give back a
// temporary value built from its internal bits.  So workarounds are needed,
// as they are for a similar situation in trying to set values inside of
// C arrays in STRUCT!.
//
// The way the workaround works involves allowing a SET-PATH! to run forward
// and write into a temporary value.  Then in these cases the temporary
// REBVAL is observed and used to write back into the native bits before the
// SET-PATH! evaluation finishes.  This means that it's not currently
// prohibited for the effect of a SET-PATH! to be writing into a temporary.
//
// Further, the `value` slot is writable...even when it is inside of the path
// that is being dispatched:
//
//     >> code: compose [(make set-path! [12-Dec-2012 day]) 1]
//     == [12-Dec-2012/day: 1]
//
//     >> do code
//
//     >> probe code
//     [1-Dec-2012/day: 1]
//
// Ren-C has largely punted on resolving these particular questions in order
// to look at "more interesting" ones.  However, names and functions have
// been updated during investigation of what was being done.
//

#include "sys-core.h"

// !!! This is defined in "tmp-evaltypes.inc", that can only be included once.
// However it contains definitions for Path_Dispatch and other things needed
// by %c-do, so it is included there and an extern used here.
//
extern const REBPEF Path_Dispatch[REB_MAX_0];


//
//  Next_Path_Throws: C
//
// Evaluate next part of a path.
//
REBOOL Next_Path_Throws(REBPVS *pvs)
{
    REBPEF dispatcher;

    REBVAL temp;

    // Path must have dispatcher, else return:
    dispatcher = Path_Dispatch[VAL_TYPE_0(pvs->value)];
    if (!dispatcher) return FALSE; // unwind, then check for errors

    pvs->item++;

    //Debug_Fmt("Next_Path: %r/%r", pvs->path-1, pvs->path);

    // Determine the "selector".  See notes on pvs->selector_temp for why
    // a local variable can't be used for the temporary space.
    //
    if (IS_GET_WORD(pvs->item)) { // e.g. object/:field
        pvs->selector
            = GET_MUTABLE_VAR_MAY_FAIL(pvs->item, pvs->item_specifier);

        if (IS_VOID(pvs->selector))
            fail (Error_No_Value_Core(pvs->item, pvs->item_specifier));

        SET_TRASH_IF_DEBUG(&pvs->selector_temp);
    }
    // object/(expr) case:
    else if (IS_GROUP(pvs->item)) {
        if (Do_At_Throws(
            &pvs->selector_temp,
            VAL_ARRAY(pvs->item),
            VAL_INDEX(pvs->item),
            IS_RELATIVE(pvs->item)
                ? pvs->item_specifier // if relative, use parent specifier...
                : VAL_SPECIFIER(const_KNOWN(pvs->item)) // ...else use child's
        )) {
            *pvs->store = pvs->selector_temp;
            return TRUE;
        }

        pvs->selector = &pvs->selector_temp;
    }
    else {
        // object/word and object/value case:
        //
        COPY_VALUE(&pvs->selector_temp, pvs->item, pvs->item_specifier);
        pvs->selector = &pvs->selector_temp;
    }

    switch (dispatcher(pvs)) {
    case PE_OK:
        break;

    case PE_SET_IF_END:
        if (pvs->opt_setval && IS_END(pvs->item + 1)) {
            *pvs->value = *pvs->opt_setval;
            pvs->opt_setval = NULL;
        }
        break;

    case PE_NONE:
        SET_BLANK(pvs->store);
    case PE_USE_STORE:
        pvs->value = pvs->store;
        pvs->value_specifier = SPECIFIED;
        break;

    default:
        assert(FALSE);
    }

    if (NOT_END(pvs->item + 1)) return Next_Path_Throws(pvs);

    return FALSE;
}


//
//  Do_Path_Throws_Core: C
//
// Evaluate an ANY_PATH! REBVAL, starting from the index position of that
// path value and continuing to the end.
//
// The evaluator may throw because GROUP! is evaluated, e.g. `foo/(throw 1020)`
//
// If label_sym is passed in as being non-null, then the caller is implying
// readiness to process a path which may be a function with refinements.
// These refinements will be left in order on the data stack in the case
// that `out` comes back as IS_FUNCTION().
//
// If `opt_setval` is given, the path operation will be done as a "SET-PATH!"
// if the path evaluation did not throw or error.  HOWEVER the set value
// is NOT put into `out`.  This provides more flexibility on performance in
// the evaluator, which may already have the `val` where it wants it, and
// so the extra assignment would just be overhead.
//
// !!! Path evaluation is one of the parts of R3-Alpha that has not been
// vetted very heavily by Ren-C, and needs a review and overhaul.
//
REBOOL Do_Path_Throws_Core(
    REBVAL *out,
    REBSTR **label_out,
    const RELVAL *path,
    REBCTX *specifier,
    REBVAL *opt_setval
) {
    REBPVS pvs;
    REBDSP dsp_orig = DSP;

    assert(ANY_PATH(path));

    // !!! There is a bug in the dispatch such that if you are running a
    // set path, it does not always assign the output, because it "thinks you
    // aren't going to look at it".  This presumably originated from before
    // parens were allowed in paths, and neglects cases like:
    //
    //     foo/(throw 1020): value
    //
    // We always have to check to see if a throw occurred.  Until this is
    // streamlined, we have to at minimum set it to something that is *not*
    // thrown so that we aren't testing uninitialized memory.  A safe trash
    // will do, which is unset in release builds.
    //
    if (opt_setval)
        SET_TRASH_SAFE(out);

    // None of the values passed in can live on the data stack, because
    // they might be relocated during the path evaluation process.
    //
    assert(!IN_DATA_STACK_DEBUG(out));
    assert(!IN_DATA_STACK_DEBUG(path));
    assert(!opt_setval || !IN_DATA_STACK_DEBUG(opt_setval));

    // Not currently robust for reusing passed in path or value as the output
    assert(out != path && out != opt_setval);

    assert(!opt_setval || !THROWN(opt_setval));

    // Initialize REBPVS -- see notes in %sys-do.h
    //
    pvs.opt_setval = opt_setval;
    pvs.store = out;
    pvs.orig = path;
    pvs.item = VAL_ARRAY_AT(pvs.orig); // may not be starting at head of PATH!

    // The path value that's coming in may be relative (in which case it
    // needs to use the specifier passed in).  Or it may be specific already,
    // in which case we should use the specifier in the value to process
    // its array contents.
    //
    if (IS_RELATIVE(path)) {
    #if !defined(NDEBUG)
        assert(specifier != SPECIFIED);

        if (VAL_RELATIVE(path) != VAL_FUNC(CTX_FRAME_FUNC_VALUE(specifier))) {
            Debug_Fmt("Specificity mismatch found in path dispatch");
            PROBE_MSG(path, "expected func");
            PROBE_MSG(CTX_FRAME_FUNC_VALUE(specifier), "actual func");
            assert(FALSE);
        }
    #endif
        pvs.item_specifier = specifier;
    }
    else pvs.item_specifier = VAL_SPECIFIER(const_KNOWN(path));

    // Seed the path evaluation process by looking up the first item (to
    // get a datatype to dispatch on for the later path items)
    //
    if (IS_WORD(pvs.item)) {
        pvs.value = GET_MUTABLE_VAR_MAY_FAIL(pvs.item, pvs.item_specifier);
        pvs.value_specifier = SPECIFIED;
        if (IS_VOID(pvs.value))
            fail (Error_No_Value_Core(pvs.item, pvs.item_specifier));
    }
    else {
        // !!! Ideally there would be some way to deal with writes to
        // temporary locations, like this pvs.value...if a set-path sets
        // it, then it will be discarded.

        COPY_VALUE(pvs.store, VAL_ARRAY_AT(pvs.orig), pvs.item_specifier);
        pvs.value = pvs.store;
        pvs.value_specifier = SPECIFIED;
    }

    // Start evaluation of path:
    if (IS_END(pvs.item + 1)) {
        // If it was a single element path, return the value rather than
        // try to dispatch it (would cause a crash at time of writing)
        //
        // !!! Is this the desired behavior, or should it be an error?
    }
    else if (Path_Dispatch[VAL_TYPE_0(pvs.value)]) {
        REBOOL threw = Next_Path_Throws(&pvs);

        // !!! See comments about why the initialization of out is necessary.
        // Without it this assertion can change on some things:
        //
        //     t: now
        //     t/time: 10:20:03
        //
        // (It thinks pvs.value has its THROWN bit set when it completed
        // successfully.  It was a PE_USE_STORE case where pvs.value was reset to
        // pvs.store, and pvs.store has its thrown bit set.  Valgrind does not
        // catch any uninitialized variables.)
        //
        // There are other cases that do trip valgrind when omitting the
        // initialization, though not as clearly reproducible.
        //
        assert(threw == THROWN(pvs.value));

        if (threw) return TRUE;

        // Check for errors:
        if (NOT_END(pvs.item + 1) && !IS_FUNCTION(pvs.value)) {
            // Only function refinements should get by this line:
            fail (Error(RE_INVALID_PATH, pvs.orig, pvs.item));
        }
    }
    else if (!IS_FUNCTION(pvs.value))
        fail (Error(RE_BAD_PATH_TYPE, pvs.orig, Type_Of(pvs.value)));

    if (opt_setval) {
        // If SET then we don't return anything
        assert(IS_END(pvs.item) + 1);
        return FALSE;
    }

    // If storage was not used, then copy final value back to it:
    if (pvs.value != pvs.store)
        COPY_VALUE(pvs.store, pvs.value, pvs.value_specifier);

    assert(!THROWN(out));

    // Return 0 if not function or is :path/word...
    if (!IS_FUNCTION(pvs.value)) {
        assert(IS_END(pvs.item) + 1);
        return FALSE;
    }

    if (label_out) {
        REBVAL refinement;

        // When a function is hit, path processing stops as soon as the
        // processed sub-path resolves to a function. The path is still sitting
        // on the position of the last component of that sub-path. Usually,
        // this last component in the sub-path is a word naming the function.
        //
        if (IS_WORD(pvs.item)) {
            *label_out = VAL_WORD_CASED(pvs.item);
        }
        else {
            // In rarer cases, the final component (completing the sub-path to
            // the function to call) is not a word. Such as when you use a path
            // to pick by index out of a block of functions:
            //
            //      functions: reduce [:add :subtract]
            //      functions/1 10 20
            //
            // Or when you have an immediate function value in a path with a
            // refinement. Tricky to make, but possible:
            //
            //      do reduce [
            //          to-path reduce [:append 'only] [a] [b]
            //      ]
            //

            // !!! When a function was not invoked through looking up a word
            // (or a word in a path) to use as a label, there were once three
            // different alternate labels used.  One was SYM__APPLY_, another
            // was ROOT_NONAME, and another was to be the type of the function
            // being executed.  None are fantastic, we do the type for now.

            *label_out = Canon(SYM_FROM_KIND(VAL_TYPE(pvs.value)));
        }

        // Move on to the refinements (if any)
        ++pvs.item;

        // !!! Currently, the mainline path evaluation "punts" on refinements.
        // When it finds a function, it stops the path evaluation and leaves
        // the position pvs.path before the list of refinements.
        //
        // A more elegant solution would be able to process and notice (for
        // instance) that `:APPEND/ONLY` should yield a function value that
        // has been specialized with a refinement.  Path chaining should thus
        // be able to effectively do this and give the refined function object
        // back to the evaluator or other client.
        //
        // If a label_sym is passed in, we recognize that a function dispatch
        // is going to be happening.  We do not want to pay to generate the
        // new series that would be needed to make a temporary function that
        // will be invoked and immediately GC'd  So we gather the refinements
        // on the data stack.
        //
        // This code simulates that path-processing-to-data-stack, but it
        // should really be something in dispatch iself.  In any case, we put
        // refinements on the data stack...and caller knows refinements are
        // from dsp_orig to DSP (thanks to accounting, all other operations
        // should balance!)

        for (; NOT_END(pvs.item); ++pvs.item) { // "the refinements"
            if (IS_VOID(pvs.item)) continue;

            if (IS_GROUP(pvs.item)) {
                //
                // Note it is not legal to use the data stack directly as the
                // output location for a DO (might be resized)

                if (Do_At_Throws(
                    &refinement,
                    VAL_ARRAY(pvs.item),
                    VAL_INDEX(pvs.item),
                    IS_RELATIVE(pvs.item)
                        ? pvs.item_specifier // if relative, use parent's
                        : VAL_SPECIFIER(const_KNOWN(pvs.item)) // else embedded
                )) {
                    *out = refinement;
                    DS_DROP_TO(dsp_orig);
                    return TRUE;
                }
                if (IS_VOID(&refinement)) continue;
                DS_PUSH(&refinement);
            }
            else if (IS_GET_WORD(pvs.item)) {
                DS_PUSH_TRASH;
                *DS_TOP = *GET_OPT_VAR_MAY_FAIL(pvs.item, pvs.item_specifier);
                if (IS_VOID(DS_TOP)) {
                    DS_DROP;
                    continue;
                }
            }
            else DS_PUSH_RELVAL(pvs.item, pvs.item_specifier);

            // Whatever we were trying to use as a refinement should now be
            // on the top of the data stack, and only words are legal ATM
            //
            if (!IS_WORD(DS_TOP)) {
                fail (Error(RE_BAD_REFINE, DS_TOP));
            }

            // Go ahead and canonize the word symbol so we don't have to
            // do it each time in order to get a case-insenstive compare
            //
            INIT_WORD_SPELLING(DS_TOP, VAL_WORD_CANON(DS_TOP));
        }

        // To make things easier for processing, reverse the refinements on
        // the data stack (we needed to evaluate them in forward order).
        // This way we can just pop them as we go, and know if they weren't
        // all consumed if it doesn't get back to `dsp_orig` by the end.

        if (dsp_orig != DSP) {
            REBVAL *bottom = DS_AT(dsp_orig + 1);
            REBVAL *top = DS_TOP;
            while (top > bottom) {
                refinement = *bottom;
                *bottom = *top;
                *top = refinement;

                top--;
                bottom++;
            }
        }
    }
    else {
        // !!! Historically this just ignores a result indicating this is a
        // function with refinements, e.g. ':append/only'.  However that
        // ignoring seems unwise.  It should presumably create a modified
        // function in that case which acts as if it has the refinement.
        //
        // If the caller did not pass in a label pointer we assume they are
        // likely not ready to process any refinements.
        //
        if (NOT_END(pvs.item + 1))
            fail (Error(RE_TOO_LONG)); // !!! Better error or add feature
    }

    return FALSE;
}


//
//  Error_Bad_Path_Select: C
//
REBCTX *Error_Bad_Path_Select(REBPVS *pvs)
{
    REBVAL orig;
    COPY_VALUE(&orig, pvs->orig, pvs->item_specifier);

    REBVAL item;
    COPY_VALUE(&item, pvs->item, pvs->item_specifier);

    return Error(RE_INVALID_PATH, &orig, &item);
}


//
//  Error_Bad_Path_Set: C
//
REBCTX *Error_Bad_Path_Set(REBPVS *pvs)
{
    REBVAL orig;
    COPY_VALUE(&orig, pvs->orig, pvs->item_specifier);

    REBVAL item;
    COPY_VALUE(&item, pvs->item, pvs->item_specifier);

    return Error(RE_BAD_PATH_SET, &orig, &item);
}


//
//  Error_Bad_Path_Range: C
//
REBCTX *Error_Bad_Path_Range(REBPVS *pvs)
{
    REBVAL item;
    COPY_VALUE(&item, pvs->item, pvs->item_specifier);

    return Error_Out_Of_Range(&item);
}


//
//  Error_Bad_Path_Field_Set: C
//
REBCTX *Error_Bad_Path_Field_Set(REBPVS *pvs)
{
    REBVAL item;
    COPY_VALUE(&item, pvs->item, pvs->item_specifier);

    return Error(RE_BAD_FIELD_SET, &item, Type_Of(pvs->opt_setval));
}


//
//  Pick_Path: C
//
// Lightweight version of Do_Path used for A_PICK actions.
// Does not do GROUP! evaluation, hence not designed to throw.
//
void Pick_Path(
    REBVAL *out,
    REBVAL *value,
    const REBVAL *selector,
    const REBVAL *opt_setval
) {
    REBPVS pvs;
    REBPEF dispatcher;

    pvs.value = value;
    pvs.value_specifier = SPECIFIED;
    pvs.item = NULL;
    pvs.selector = selector;
    pvs.opt_setval = opt_setval;
    pvs.store = out;        // Temp space for constructed results

    // Path must have dispatcher, else return:
    dispatcher = Path_Dispatch[VAL_TYPE_0(value)];
    if (!dispatcher) return; // unwind, then check for errors

    switch (dispatcher(&pvs)) {
    case PE_OK:
        break;

    case PE_SET_IF_END: // !!! Said "only sets if end of path", but no check?
        if (pvs.opt_setval)
            *pvs.value = *pvs.opt_setval;
        break;

    case PE_NONE:
        SET_BLANK(pvs.store);
    case PE_USE_STORE:
        pvs.value = pvs.store;
        pvs.value_specifier = SPECIFIED;
        break;

    default:
        assert(FALSE);
    }
}


//
//  Get_Simple_Value_Into: C
//
// Does easy lookup, else just returns the value as is.
//
void Get_Simple_Value_Into(REBVAL *out, const RELVAL *val, REBCTX *specifier)
{
    if (IS_WORD(val) || IS_GET_WORD(val)) {
        *out = *GET_OPT_VAR_MAY_FAIL(val, specifier);
    }
    else if (IS_PATH(val) || IS_GET_PATH(val)) {
        if (Do_Path_Throws_Core(out, NULL, val, specifier, NULL))
            fail (Error_No_Catch_For_Throw(out));
    }
    else {
        COPY_VALUE(out, val, specifier);
    }
}


//
//  Resolve_Path: C
//
// Given a path, determine if it is ultimately specifying a selection out
// of a context...and if it is, return that context.  So `a/obj/key` would
// return the object assocated with obj, while `a/str/1` would return
// NULL if `str` were a string as it's not an object selection.
//
// !!! This routine overlaps the logic of Do_Path, and should potentially
// be a mode of that instead.  It is not very complete, considering that it
// does not execute GROUP! (and perhaps shouldn't?) and only supports a
// path that picks contexts out of other contexts, via word selection.
//
REBCTX *Resolve_Path(const REBVAL *path, REBCNT *index_out)
{
    RELVAL *selector;
    const REBVAL *var;
    REBARR *array;
    REBCNT i;

    array = VAL_ARRAY(path);
    selector = ARR_HEAD(array);

    if (IS_END(selector) || !ANY_WORD(selector))
        return NULL; // !!! only handles heads of paths that are ANY-WORD!

    var = GET_OPT_VAR_MAY_FAIL(selector, VAL_SPECIFIER(path));

    ++selector;
    if (IS_END(selector))
        return NULL; // !!! does not handle single-element paths

    while (ANY_CONTEXT(var) && IS_WORD(selector)) {
        i = Find_Canon_In_Context(
            VAL_CONTEXT(var), VAL_WORD_CANON(selector), FALSE
        );
        ++selector;
        if (IS_END(selector)) {
            *index_out = i;
            return VAL_CONTEXT(var);
        }

        var = CTX_VAR(VAL_CONTEXT(var), i);
    }

    DEAD_END;
}
