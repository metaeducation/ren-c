//
//  File: %c-path.h
//  Summary: "Core Path Dispatching and Chaining"
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
// !!! See notes in %sys-path.h regarding the R3-Alpha path dispatch concept
// and regarding areas that need improvement.
//

#include "sys-core.h"


//
//  PD_Fail: C
//
// In order to avoid having to pay for a check for NULL in the path dispatch
// table for types with no path dispatch, a failing handler is in the slot.
//
REBINT PD_Fail(REBPVS *pvs)
{
    DECLARE_LOCAL (specified_orig);
    Derelativize(specified_orig, pvs->orig, pvs->item_specifier);

    DECLARE_LOCAL (specified_item);
    Derelativize(specified_item, pvs->item, pvs->item_specifier);

    fail (Error_Invalid_Path_Raw(specified_orig, specified_item));
}


//
//  Next_Path_Throws: C
//
// Evaluate next part of a path.
//
// !!! This is done as a recursive function instead of iterating in a loop due
// to the unusual nature of some path dispatches that call Next_Path_Throws()
// inside their implementation.
//
REBOOL Next_Path_Throws(REBPVS *pvs)
{
    REBPEF dispatcher = Path_Dispatch[VAL_TYPE(pvs->value)];
    assert(dispatcher != NULL); // &PD_Fail is used instead of NULL

    if (IS_FUNCTION(pvs->value) && pvs->label_out && *pvs->label_out == NULL)
        if (IS_WORD(pvs->item))
            *pvs->label_out = VAL_WORD_SPELLING(pvs->item);

    pvs->item++;

    // Calculate the "selector" into the GC guarded cell.
    //
    assert(pvs->selector == &pvs->selector_cell);

    if (IS_GET_WORD(pvs->item)) { // e.g. object/:field
        Copy_Opt_Var_May_Fail(
            &pvs->selector_cell, pvs->item, pvs->item_specifier
        );

        if (IS_VOID(pvs->selector))
            fail (Error_No_Value_Core(pvs->item, pvs->item_specifier));
    }
    else if (IS_GROUP(pvs->item)) { // object/(expr) case:
        REBSPC *derived = Derive_Specifier(pvs->item_specifier, pvs->item);
        if (Do_At_Throws(
            &pvs->selector_cell,
            VAL_ARRAY(pvs->item),
            VAL_INDEX(pvs->item),
            derived
        )) {
            Move_Value(pvs->store, &pvs->selector_cell);
            return TRUE;
        }
    }
    else { // object/word and object/value case:
        Derelativize(&pvs->selector_cell, pvs->item, pvs->item_specifier);
    }

    // Disallow voids from being used in path dispatch.  This rule seems like
    // common sense for safety, and also corresponds to voids being illegal
    // to use in SELECT.
    //
    if (IS_VOID(pvs->selector))
        fail (Error_No_Value_Core(pvs->item, pvs->item_specifier));

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
        Init_Blank(pvs->store);
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
    REBSPC *specifier,
    REBVAL *opt_setval
) {
    // The pvs contains a cell for the selector into which evaluations are
    // done, e.g. `foo/(1 + 2)`.  Because Next_Path() doesn't commit to not
    // performing any evaluations this cell must be guarded.  In the case of
    // a fail() this guard will be released automatically, but to return
    // normally use `return_thrown` and `return_not_thrown` which drops guard.
    //
    // !!! There was also a strange requirement in some more quirky path
    // evaluation (GOB!, STRUCT!) that the cell survive between Next_Path()
    // calls, which may still be relevant to why this can't be a C local.
    //
    REBPVS pvs;
    Prep_Global_Cell(&pvs.selector_cell);
    Init_End(&pvs.selector_cell);
    PUSH_GUARD_VALUE(&pvs.selector_cell);
    pvs.selector = &pvs.selector_cell;

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
        Init_Unreadable_Blank(out);

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
    pvs.label_out = label_out;
    if (label_out != NULL)
        *label_out = NULL; // initial value if no function label found

    // The path value that's coming in may be relative (in which case it
    // needs to use the specifier passed in).  Or it may be specific already,
    // in which case we should use the specifier in the value to process
    // its array contents.
    //
    if (IS_RELATIVE(path)) {
    #if !defined(NDEBUG)
        assert(specifier != SPECIFIED);

        REBCTX *context = AS_CONTEXT(specifier);
        if (VAL_RELATIVE(path) != VAL_FUNC(CTX_FRAME_FUNC_VALUE(context))) {
            printf("Specificity mismatch in path dispatch, expected:\n");
            PROBE(CTX_FRAME_FUNC_VALUE(context));
            printf("Panic on actual path\n");
            panic (path);
        }
    #endif
        pvs.item_specifier = specifier;
    }
    else pvs.item_specifier = VAL_SPECIFIER(const_KNOWN(path));

    // Seed the path evaluation process by looking up the first item (to
    // get a datatype to dispatch on for the later path items)
    //
    if (IS_WORD(pvs.item)) {
        pvs.value = Get_Mutable_Var_May_Fail(pvs.item, pvs.item_specifier);
        pvs.value_specifier = SPECIFIED;
        if (IS_VOID(pvs.value))
            fail (Error_No_Value_Core(pvs.item, pvs.item_specifier));
    }
    else {
        // !!! Ideally there would be some way to deal with writes to
        // temporary locations, like this pvs.value...if a set-path sets
        // it, then it will be discarded.

        Derelativize(pvs.store, VAL_ARRAY_AT(pvs.orig), pvs.item_specifier);
        pvs.value = pvs.store;
        pvs.value_specifier = SPECIFIED;
    }

    // Start evaluation of path:
    if (IS_END(pvs.item + 1)) {
        // If it was a single element path, return the value rather than
        // try to dispatch it (would cause a crash at time of writing)
        //
        // !!! Is this the desired behavior, or should it be an error?

        if (IS_FUNCTION(pvs.value) && IS_WORD(pvs.item) && pvs.label_out)
            *pvs.label_out = VAL_WORD_SPELLING(pvs.item);
    }
    else {
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

        if (threw)
            goto return_thrown;
    }

    if (opt_setval) {
        // If SET then we don't return anything
        assert(IS_END(pvs.item) + 1);
        goto return_not_thrown;
    }

    // If storage was not used, then copy final value back to it:
    if (pvs.value != pvs.store)
        Derelativize(pvs.store, pvs.value, pvs.value_specifier);

    assert(!THROWN(out));

    assert(IS_END(pvs.item) + 1);

    // To make things easier for processing, reverse any refinements
    // pushed to the data stack (we needed to evaluate them
    // in forward order).  This way we can just pop them as we go,
    // and know if they weren't all consumed if it doesn't get
    // back to `dsp_orig` by the end.
    //
    if (dsp_orig != DSP) {
        assert(IS_FUNCTION(pvs.store));

        // !!! It should be technically possible to do something like
        // :append/dup and return a "refined" variant of a function.  That
        // feature is not currently implemented.  So if a label wasn't
        // requested, assume a function is not being run and deliver an
        // error for that case.
        //
        if (label_out == NULL)
            fail (Error_Too_Long_Raw());

        REBVAL *bottom = DS_AT(dsp_orig + 1);
        REBVAL *top = DS_TOP;
        while (top > bottom) {
            DECLARE_LOCAL (temp);
            Move_Value(temp, bottom);
            Move_Value(bottom, top);
            Move_Value(top, temp);

            top--;
            bottom++;
        }
    }

return_not_thrown:
    DROP_GUARD_VALUE(&pvs.selector_cell);
    return FALSE;

return_thrown:
    DROP_GUARD_VALUE(&pvs.selector_cell);
    return TRUE;
}


//
//  Error_Bad_Path_Select: C
//
REBCTX *Error_Bad_Path_Select(REBPVS *pvs)
{
    DECLARE_LOCAL (orig);
    Derelativize(orig, pvs->orig, pvs->item_specifier);

    DECLARE_LOCAL (item);
    Derelativize(item, pvs->item, pvs->item_specifier);

    return Error_Invalid_Path_Raw(orig, item);
}


//
//  Error_Bad_Path_Set: C
//
REBCTX *Error_Bad_Path_Set(REBPVS *pvs)
{
    DECLARE_LOCAL (orig);
    Derelativize(orig, pvs->orig, pvs->item_specifier);

    DECLARE_LOCAL (item);
    Derelativize(item, pvs->item, pvs->item_specifier);

    return Error_Bad_Path_Set_Raw(orig, item);
}


//
//  Error_Bad_Path_Range: C
//
REBCTX *Error_Bad_Path_Range(REBPVS *pvs)
{
    DECLARE_LOCAL (item);
    Derelativize(item, pvs->item, pvs->item_specifier);

    return Error_Out_Of_Range(item);
}


//
//  Error_Bad_Path_Field_Set: C
//
REBCTX *Error_Bad_Path_Field_Set(REBPVS *pvs)
{
    DECLARE_LOCAL (item);
    Derelativize(item, pvs->item, pvs->item_specifier);

    return Error_Bad_Field_Set_Raw(item, Type_Of(pvs->opt_setval));
}


//
//  Get_Simple_Value_Into: C
//
// Does easy lookup, else just returns the value as is.
//
void Get_Simple_Value_Into(REBVAL *out, const RELVAL *val, REBSPC *specifier)
{
    if (IS_WORD(val) || IS_GET_WORD(val)) {
        Copy_Opt_Var_May_Fail(out, val, specifier);
    }
    else if (IS_PATH(val) || IS_GET_PATH(val)) {
        if (Do_Path_Throws_Core(out, NULL, val, specifier, NULL))
            fail (Error_No_Catch_For_Throw(out));
    }
    else {
        Derelativize(out, val, specifier);
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
    const RELVAL *var;
    REBARR *array;
    REBCNT i;

    array = VAL_ARRAY(path);
    selector = ARR_HEAD(array);

    if (IS_END(selector) || !ANY_WORD(selector))
        return NULL; // !!! only handles heads of paths that are ANY-WORD!

    var = Get_Opt_Var_May_Fail(selector, VAL_SPECIFIER(path));

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
