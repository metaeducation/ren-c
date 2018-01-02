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
REB_R PD_Fail(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    UNUSED(pvs);
    UNUSED(picker);
    UNUSED(opt_setval);

    return R_UNHANDLED;
}


//
//  PD_Unhooked: C
//
// As a temporary workaround for not having real user-defined types, an
// extension can overtake an "unhooked" type slot to provide behavior.
//
REB_R PD_Unhooked(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    UNUSED(pvs);
    UNUSED(picker);
    UNUSED(opt_setval);

    REBVAL *type = Get_Type(VAL_TYPE(pvs->out)); // put in error message?
    UNUSED(type);

    fail ("Datatype is provided by an extension which is not loaded.");
}


//
//  Next_Path_Throws: C
//
// Evaluate next part of a path.
//
// !!! This is done as a recursive function instead of iterating in a loop due
// to the unusual nature of some path dispatches that call Next_Path_Throws()
// inside their implementation.  Those two cases (FFI array writeback and
// writing GOB x and y coordinates) are intended to be revisited after this
// code gets more reorganized.
//
REBOOL Next_Path_Throws(REBPVS *pvs)
{
    if (IS_VOID(pvs->out))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    REBPEF dispatcher = Path_Dispatch[VAL_TYPE(pvs->out)];
    assert(dispatcher != NULL); // &PD_Fail is used instead of NULL

    // Calculate the "picker" into the GC guarded cell.
    //
    assert(pvs->refine == &pvs->cell);

    if (IS_GET_WORD(pvs->value)) { // e.g. object/:field
        Copy_Opt_Var_May_Fail(
            SINK(&pvs->cell), pvs->value, pvs->specifier
        );
    }
    else if (IS_GROUP(pvs->value)) { // object/(expr) case:
        if (pvs->flags.bits & DO_FLAG_NEUTRAL) {
            Move_Value(pvs->out, BLANK_VALUE);
            CONVERT_NAME_TO_THROWN(pvs->out, BAR_VALUE);
            return TRUE;
        }

        REBSPC *derived = Derive_Specifier(pvs->specifier, pvs->value);
        if (Do_At_Throws(
            SINK(&pvs->cell),
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            derived
        )) {
            Move_Value(pvs->out, KNOWN(&pvs->cell));
            return TRUE;
        }
    }
    else { // object/word and object/value case:
        Derelativize(&pvs->cell, pvs->value, pvs->specifier);
    }

    // Disallow voids from being used in path dispatch.  This rule seems like
    // common sense for safety, and also corresponds to voids being illegal
    // to use in SELECT.
    //
    if (IS_VOID(pvs->refine))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    Fetch_Next_In_Frame(pvs); // may be at end

    REB_R r;

    if (
        FRM_AT_END(pvs)
        && pvs->eval_type == REB_SET_PATH
    ){
        const REBVAL *opt_setval = pvs->special;
        assert(opt_setval != NULL);

        switch (dispatcher(pvs, pvs->refine, opt_setval)) {
        case R_INVISIBLE: // dispatcher assigned target with opt_setval
            if (pvs->flags.bits & DO_FLAG_SET_PATH_ENFIXED)
                fail ("Path setting was not via an enfixable reference");
            break; // nothing left to do, have to take the dispatcher's word

        case R_REFERENCE: { // dispatcher wants us to set *if* at end of path
            assert(VAL_TYPE(pvs->out) == REB_0_REFERENCE);
            Move_Value(VAL_REFERENCE(pvs->out), pvs->special);

            if (pvs->flags.bits & DO_FLAG_SET_PATH_ENFIXED) {
                assert(IS_FUNCTION(pvs->special));
                SET_VAL_FLAG(VAL_REFERENCE(pvs->out), VALUE_FLAG_ENFIXED);
            }
            break; }

        case R_IMMEDIATE: {
            //
            // Imagine something like:
            //
            //      month/year: 1
            //
            // First month is written into the out slot as a reference to the
            // location of the month DATE! variable.  But because we don't
            // pass references from the previous steps *in* to the path
            // picking material, it only has the copied value in pvs->out.
            //
            // If we had a reference before we called in, we saved it in
            // pvs->deferred.  So in the example case of `month/year:`, that
            // would be the CTX_VAR() where month was found initially, and so
            // we write the updated bits from pvs->out there.

            if (pvs->flags.bits & DO_FLAG_SET_PATH_ENFIXED)
                fail ("Can't enfix a write into an immediate value");

            if (pvs->deferred == NULL)
                fail ("Can't update temporary immediate value via SET-PATH!");

            Move_Value(pvs->deferred, pvs->out);
            break; }

        case R_UNHANDLED:
            fail (Error_Bad_Path_Poke_Raw(pvs->refine));

        default:
            //
            // Something like an R_VOID or generic R_OUT.  We could in theory
            // take those to just be variations of R_IMMEDIATE, but it's safer
            // to break that out as a separate class.
            //
            fail ("Path evaluation produced temporary value, can't POKE it");
        }
        TRASH_POINTER_IF_DEBUG(pvs->special);
    }
    else {
        const REBVAL *opt_setval = NULL;
        r = dispatcher(pvs, pvs->refine, opt_setval);

        pvs->deferred = NULL; // clear status of the deferred

        switch (r) {
        case R_INVISIBLE:
            assert(pvs->eval_type == REB_SET_PATH);
            if (
                dispatcher != Path_Dispatch[REB_STRUCT]
                && dispatcher != Path_Dispatch[REB_GOB]
            ){
                panic("SET-PATH! evaluation ran assignment before path end");
            }

            // !!! Temporary exception for STRUCT! and GOB!, the hack the
            // dispatcher uses to do "sub-value addressing" is to call
            // Next_Path_Throws inside of them, to be able to do a write
            // while they still have memory of what the struct and variable
            // are (which would be lost in this protocol otherwise).
            //
            assert(FRM_AT_END(pvs));
            break;

        case R_REFERENCE:
            assert(VAL_TYPE(pvs->out) == REB_0_REFERENCE);

            // Save the reference location in case the next update turns out
            // to be R_IMMEDIATE, and we need it.  Not actually KNOWN() but
            // we are only going to use it as a sink for data...if we use it.
            //
            pvs->deferred = cast(REBVAL*, VAL_REFERENCE(pvs->out));

            Derelativize(
                pvs->out, VAL_REFERENCE(pvs->out), VAL_SPECIFIER(pvs->out)
            );
            break;

        case R_VOID:
            Init_Void(pvs->out);
            break;

        case R_BLANK:
            Init_Blank(pvs->out);
            break;

        case R_OUT:
            break;

        case R_UNHANDLED:
            fail (Error_Bad_Path_Pick_Raw(pvs->refine));

        default:
            assert(FALSE);
        }
    }

    // A function being refined does not actually update pvs->out with
    // a "more refined" function value, it holds the original function and
    // accumulates refinement state on the stack.  The label should only
    // be captured the first time the function is seen, otherwise it would
    // capture the last refinement's name, so check label for non-NULL.
    //
    if (IS_FUNCTION(pvs->out) && IS_WORD(pvs->refine)) {
        if (pvs->opt_label == NULL)
            pvs->opt_label = VAL_WORD_SPELLING(pvs->refine);
    }

    if (FRM_AT_END(pvs))
        return FALSE; // did not throw

    return Next_Path_Throws(pvs);
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
    enum Reb_Kind kind,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    const REBVAL *opt_setval,
    REBFLGS flags
){
    assert(kind == REB_PATH || kind == REB_SET_PATH || kind == REB_GET_PATH);

    DECLARE_FRAME (pvs);

    pvs->refine = KNOWN(&pvs->cell);

    Push_Frame_At(
        pvs,
        array,
        index,
        specifier,
        flags
    );

    if (FRM_AT_END(pvs))
        fail ("Cannot dispatch empty path");

    pvs->eval_type = kind;

    // Push_Frame_At sets the output to the global unwritable END cell, so we
    // have to wait for this point to set to the output cell we want.
    //
    pvs->out = out;
    SET_END(out);

    REBDSP dsp_orig = DSP;

    // None of the values passed in can live on the data stack, because
    // they might be relocated during the path evaluation process.
    //
    assert(opt_setval == NULL || !IN_DATA_STACK_DEBUG(opt_setval));

    // Not robust for reusing passed in value as the output
    assert(out != opt_setval);

    // Initialize REBPVS -- see notes in %sys-do.h
    //
    pvs->special = opt_setval;
    pvs->opt_label = NULL;
    pvs->deferred = NULL;

    // Seed the path evaluation process by looking up the first item (to
    // get a datatype to dispatch on for the later path items)
    //
    if (IS_WORD(pvs->value)) {
        //
        // Remember the actual location of this variable, not just its value,
        // in case we need to do R_IMMEDIATE writeback (e.g. month/day: 1)
        //
        pvs->deferred = Get_Mutable_Var_May_Fail(pvs->value, pvs->specifier);

        Move_Value(pvs->out, pvs->deferred);

        if (IS_FUNCTION(pvs->out))
            pvs->opt_label = VAL_WORD_SPELLING(pvs->value);
    }
    else if (IS_GROUP(pvs->value)) {
        if (pvs->flags.bits & DO_FLAG_NEUTRAL) {
            Move_Value(pvs->out, BLANK_VALUE);
            CONVERT_NAME_TO_THROWN(pvs->out, BAR_VALUE);
            goto return_thrown;
        }

        REBSPC *derived = Derive_Specifier(pvs->specifier, pvs->value);
        if (Do_At_Throws(
            pvs->out,
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            derived
        )){
            goto return_thrown;
        }

        pvs->deferred = NULL; // nowhere to R_IMMEDIATE write back to
    }
    else {
        Derelativize(pvs->out, pvs->value, pvs->specifier);

        pvs->deferred = NULL; // nowhere to R_IMMEDIATE write back to
    }

    if (IS_VOID(pvs->out))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    Fetch_Next_In_Frame(pvs);

    if (FRM_AT_END(pvs)) {
        // If it was a single element path, return the value rather than
        // try to dispatch it (would cause a crash at time of writing)
        //
        // !!! Is this the desired behavior, or should it be an error?
    }
    else {
        if (Next_Path_Throws(pvs))
            goto return_thrown;
    }

    assert(FRM_AT_END(pvs));

    if (opt_setval) {
        // If SET then we don't return anything
        goto return_not_thrown;
    }

    assert(!THROWN(out));

    // To make things easier for processing, reverse any refinements
    // pushed to the data stack (we needed to evaluate them
    // in forward order).  This way we can just pop them as we go,
    // and know if they weren't all consumed if it doesn't get
    // back to `dsp_orig` by the end.
    //
    if (dsp_orig != DSP) {
        assert(IS_FUNCTION(pvs->out));

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
    if (label_out != NULL)
        *label_out = pvs->opt_label;

    Abort_Frame_Core(pvs);

#if !defined(NDEBUG)
    if (kind == REB_SET_PATH)
        TRASH_CELL_IF_DEBUG(out);
    else
        assert(NOT(THROWN(out)));
#endif
    return FALSE;

return_thrown:
    Abort_Frame_Core(pvs);

    assert(THROWN(out));
    return TRUE;
}


//
//  Get_Simple_Value_Into: C
//
// "Does easy lookup, else just returns the value as is."
//
// !!! This is a questionable service, reminiscent of old behaviors of GET,
// were `get x` would look up a variable but `get 3` would give you 3.
// At time of writing it seems to appear in only two places.
//
void Get_Simple_Value_Into(REBVAL *out, const RELVAL *val, REBSPC *specifier)
{
    if (IS_WORD(val) || IS_GET_WORD(val))
        Copy_Opt_Var_May_Fail(out, val, specifier);
    else if (IS_PATH(val) || IS_GET_PATH(val))
        Get_Path_Core(out, val, specifier);
    else
        Derelativize(out, val, specifier);
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
    REBARR *array = VAL_ARRAY(path);
    RELVAL *picker = ARR_HEAD(array);

    if (IS_END(picker) || !ANY_WORD(picker))
        return NULL; // !!! only handles heads of paths that are ANY-WORD!

    const RELVAL *var = Get_Opt_Var_May_Fail(picker, VAL_SPECIFIER(path));

    ++picker;
    if (IS_END(picker))
        return NULL; // !!! does not handle single-element paths

    while (ANY_CONTEXT(var) && IS_WORD(picker)) {
        REBCNT i = Find_Canon_In_Context(
            VAL_CONTEXT(var), VAL_WORD_CANON(picker), FALSE
        );
        ++picker;
        if (IS_END(picker)) {
            *index_out = i;
            return VAL_CONTEXT(var);
        }

        var = CTX_VAR(VAL_CONTEXT(var), i);
    }

    return NULL;
}


//
//  pick*: native [
//
//  {Perform a path picking operation, same as `:(:location)/(:picker)`}
//
//      return: [<opt> any-value!]
//          {Picked value, or void if picker can't fulfill the request}
//      location [any-value!]
//      picker [any-value!]
//          {Index offset, symbol, or other value to use as index}
//  ]
//
REBNATIVE(pick_p)
//
// In R3-Alpha, PICK was an "action", which dispatched on types through the
// "action mechanic" for the following types:
//
//     [any-series! map! gob! pair! date! time! tuple! bitset! port! varargs!]
//
// In Ren-C, PICK is rethought to use the same dispatch mechanic as paths,
// to cut down on the total number of operations the system has to define.
{
    INCLUDE_PARAMS_OF_PICK_P;

    REBVAL *location = ARG(location);

    // PORT!s are kind of a "user defined type" which historically could
    // react to PICK and POKE, but which could not override path dispatch.
    // Use a symbol-based call to bounce the frame to the port, which should
    // be a compatible frame with the historical "action".
    //
    if (IS_PORT(location))
        return Do_Port_Action(frame_, VAL_CONTEXT(location), SYM_PICK_P);

    DECLARE_FRAME (pvs);

    Move_Value(D_OUT, location);
    pvs->out = D_OUT;

    // !!! Sometimes path dispatchers check the item to see if it's at the
    // end of the path.  The entire thing needs review.  In the meantime,
    // take advantage of the implicit termination of the frame cell.
    //
    Move_Value(D_CELL, ARG(picker));
    assert(IS_END(D_CELL + 1));
    pvs->refine = D_CELL;

    pvs->value = D_CELL;
    pvs->specifier = SPECIFIED;

    pvs->opt_label = NULL; // applies to e.g. :append/only returning APPEND
    pvs->special = NULL;

    REBPEF dispatcher = Path_Dispatch[VAL_TYPE(location)];
    assert(dispatcher != NULL); // &PD_Fail is used instead of NULL

    REB_R r = dispatcher(pvs, ARG(picker), NULL);
    switch (r) {
    case R_INVISIBLE:
        assert(FALSE); // only SETs should do this
        break;

    case R_REFERENCE:
        Derelativize(D_OUT, VAL_REFERENCE(D_OUT), VAL_SPECIFIER(D_OUT));
        return R_OUT;

    case R_UNHANDLED:
        fail (Error_Bad_Path_Pick_Raw(ARG(picker)));

    default:
        break;
    }

    return r;
}


//
//  poke: native [
//
//  {Perform a path poking operation, same as `(:location)/(:picker): :value`}
//
//      return: [<opt> any-value!]
//          {Same as value}
//      location [any-value!]
//          {(modified)}
//      picker
//          {Index offset, symbol, or other value to use as index}
//      value [<opt> any-value!]
//          {The new value}
//  ]
//
REBNATIVE(poke)
//
// As with PICK*, POKE is changed in Ren-C from its own action to "whatever
// path-setting (now path-poking) would do".
{
    INCLUDE_PARAMS_OF_POKE;

    REBVAL *location = ARG(location);

    // PORT!s are kind of a "user defined type" which historically could
    // react to PICK and POKE, but which could not override path dispatch.
    // Use a symbol-based call to bounce the frame to the port, which should
    // be a compatible frame with the historical "action".
    //
    if (IS_PORT(location))
        return Do_Port_Action(frame_, VAL_CONTEXT(location), SYM_POKE);

    DECLARE_FRAME (pvs);

    Move_Value(D_OUT, location);
    pvs->out = D_OUT;

    // !!! Sometimes the path mechanics do the writes for a poke inside their
    // dispatcher, vs. delegating via R_REFERENCE.  They check to see if
    // the current pvs->value is at the end.  All of path dispatch was ad hoc
    // and needs a review.  In the meantime, take advantage of the implicit
    // termination of the frame cell.
    //
    Move_Value(D_CELL, ARG(picker));
    assert(IS_END(D_CELL + 1));
    pvs->refine = D_CELL;

    pvs->value = D_CELL;
    pvs->specifier = SPECIFIED;

    pvs->opt_label = NULL; // applies to e.g. :append/only returning APPEND
    pvs->special = ARG(value);

    REBPEF dispatcher = Path_Dispatch[VAL_TYPE(location)];
    assert(dispatcher != NULL); // &PD_Fail is used instead of NULL

    REB_R r = dispatcher(pvs, ARG(picker), ARG(value));
    switch (r) {
    case R_REFERENCE: // wants us to write it
        Move_Value(VAL_REFERENCE(D_OUT), ARG(value));
        break;

    case R_INVISIBLE: // is saying it did the write already
        break;

    case R_UNHANDLED:
        fail (Error_Bad_Path_Poke_Raw(ARG(picker)));

    default:
        assert(FALSE);
        fail (ARG(picker)); // Invalid argument
    }

    Move_Value(D_OUT, ARG(value)); // return the value we got in
    return R_OUT;
}
