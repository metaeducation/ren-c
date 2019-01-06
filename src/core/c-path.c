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
//  Init_Any_Path_At_Core: C
//
REBVAL *Init_Any_Path_At_Core(
    RELVAL *out,
    enum Reb_Kind kind,
    REBARR *a,
    REBCNT index,
    REBNOD *binding
){
    assert(ANY_PATH_KIND(kind));
    ENSURE_SERIES_MANAGED(SER(a));
    ASSERT_SERIES_TERM(SER(a));
    assert(index == 0); // !!! current rule
 
    RESET_CELL(out, kind);
    PAYLOAD(Series, out).rebser = SER(a);
    VAL_INDEX(out) = index;
    INIT_BINDING(out, binding);

    if (ARR_LEN(a) < 2)
        panic (a);

    return KNOWN(out);
}


//
//  PD_Fail: C
//
// In order to avoid having to pay for a check for NULL in the path dispatch
// table for types with no path dispatch, a failing handler is in the slot.
//
REB_R PD_Fail(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    UNUSED(picker);
    UNUSED(opt_setval);

    fail (pvs->out);
}


//
//  PD_Unhooked: C
//
// As a temporary workaround for not having real user-defined types, an
// extension can overtake an "unhooked" type slot to provide behavior.
//
REB_R PD_Unhooked(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    UNUSED(picker);
    UNUSED(opt_setval);

    const REBVAL *type = Datatype_From_Kind(VAL_TYPE(pvs->out));
    UNUSED(type); // !!! put in error message?

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
bool Next_Path_Throws(REBPVS *pvs)
{
    if (IS_NULLED(pvs->out))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    if (IS_GET_WORD(pvs->value)) { // e.g. object/:field
        Move_Opt_Var_May_Fail(PVS_PICKER(pvs), pvs->value, pvs->specifier);
    }
    else if (
        IS_GROUP(pvs->value) // object/(expr) case:
        and NOT_EVAL_FLAG(pvs, PATH_HARD_QUOTE) // not precomposed
    ){
        if (GET_EVAL_FLAG(pvs, NO_PATH_GROUPS))
            fail ("GROUP! in PATH! used with GET or SET (use REDUCE/EVAL)");

        REBSPC *derived = Derive_Specifier(pvs->specifier, pvs->value);
        if (Do_At_Throws(
            PVS_PICKER(pvs),
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            derived
        )) {
            Move_Value(pvs->out, PVS_PICKER(pvs));
            return true; // thrown
        }
    }
    else { // object/word and object/value case:
        Derelativize(PVS_PICKER(pvs), pvs->value, pvs->specifier);
    }

    // Disallow voids from being used in path dispatch.  This rule seems like
    // common sense for safety, and also corresponds to voids being illegal
    // to use in SELECT.
    //
    if (IS_NULLED(PVS_PICKER(pvs)))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    Fetch_Next_In_Frame(nullptr, pvs); // may be at end

  redo:;

    enum Reb_Kind kind = VAL_TYPE(pvs->out);
    PATH_HOOK hook = Path_Hooks[kind]; // &PD_Fail is used instead of NULL

    if (IS_END(pvs->value) and PVS_IS_SET_PATH(pvs)) {
        const REBVAL *r = hook(
            pvs,
            PVS_PICKER(pvs),
            PVS_OPT_SETVAL(pvs)
        );

        switch (KIND_BYTE(r)) {
          case REB_0_END: // unhandled
            assert(r == R_UNHANDLED); // shouldn't be other ends
            fail (Error_Bad_Path_Poke_Raw(PVS_PICKER(pvs)));

          case REB_R_THROWN:
            panic ("Path dispatch isn't allowed to throw, only GROUP!s");

          case REB_R_INVISIBLE: // dispatcher assigned target with opt_setval
            if (GET_EVAL_FLAG(pvs, SET_PATH_ENFIXED))
                fail ("Path setting was not via an enfixable reference");
            break; // nothing left to do, have to take the dispatcher's word

          case REB_R_REFERENCE: { // dispatcher wants a set *if* at end of path
            Move_Value(pvs->u.ref.cell, PVS_OPT_SETVAL(pvs));

            if (GET_EVAL_FLAG(pvs, SET_PATH_ENFIXED)) {
                assert(IS_ACTION(PVS_OPT_SETVAL(pvs)));
                SET_CELL_FLAG(pvs->u.ref.cell, ENFIXED);
            }
            break; }

          case REB_R_IMMEDIATE: {
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
            // pvs->u.ref.  So in the example case of `month/year:`, that
            // would be the CTX_VAR() where month was found initially, and so
            // we write the updated bits from pvs->out there.

            if (GET_EVAL_FLAG(pvs, SET_PATH_ENFIXED))
                fail ("Can't enfix a write into an immediate value");

            if (not pvs->u.ref.cell)
                fail ("Can't update temporary immediate value via SET-PATH!");

            Move_Value(pvs->u.ref.cell, pvs->out);
            break; }

          case REB_R_REDO: // e.g. used by REB_QUOTED to retrigger, sometimes
            goto redo;

          default:
            //
            // Something like a generic D_OUT.  We could in theory take those
            // to just be variations of R_IMMEDIATE, but it's safer to break
            // that out as a separate class.
            //
            fail ("Path evaluation produced temporary value, can't POKE it");
        }
        TRASH_POINTER_IF_DEBUG(pvs->special);
    }
    else {
        pvs->u.ref.cell = nullptr; // clear status of the reference

        const REBVAL *r = hook(
            pvs,
            PVS_PICKER(pvs),
            nullptr // no opt_setval, GET-PATH! or a SET-PATH! not at the end
        );

        if (r and r != END_NODE) {
            assert(r->header.bits & NODE_FLAG_CELL);
            /* assert(not (r->header.bits & NODE_FLAG_ROOT)); */
        }

        if (r == pvs->out) {
            // Common case... result where we expect it
        }
        else if (not r) {
            Init_Nulled(pvs->out);
        }
        else if (r == R_UNHANDLED) {
            fail (Error_Bad_Path_Pick_Raw(PVS_PICKER(pvs)));
        }
        else if (GET_CELL_FLAG(r, ROOT)) { // API, from Alloc_Value()
            Handle_Api_Dispatcher_Result(pvs, r);
        }
        else switch (KIND_BYTE(r)) {
          case REB_R_THROWN:
            panic ("Path dispatch isn't allowed to throw, only GROUP!s");

          case REB_R_INVISIBLE:
            assert(PVS_IS_SET_PATH(pvs));
            if (
                hook != Path_Hooks[REB_STRUCT]
                and hook != Path_Hooks[REB_GOB]
            ){
                panic("SET-PATH! evaluation ran assignment before path end");
            }

            // !!! Temporary exception for STRUCT! and GOB!, the hack the
            // dispatcher uses to do "sub-value addressing" is to call
            // Next_Path_Throws inside of them, to be able to do a write
            // while they still have memory of what the struct and variable
            // are (which would be lost in this protocol otherwise).
            //
            assert(IS_END(pvs->value));
            break;

          case REB_R_REFERENCE: {
            bool was_const = GET_CELL_FLAG(pvs->out, CONST);
            Derelativize(
                pvs->out,
                pvs->u.ref.cell,
                pvs->u.ref.specifier
            );
            if (was_const) // can't Inherit_Const(), flag would be overwritten
                SET_CELL_FLAG(pvs->out, CONST);
            if (GET_CELL_FLAG(pvs->u.ref.cell, ENFIXED))
                SET_CELL_FLAG(pvs->out, ENFIXED);

            // Leave the pvs->u.ref as-is in case the next update turns out
            // to be R_IMMEDIATE, and it is needed.
            break; }

          case REB_R_REDO: // e.g. used by REB_QUOTED to retrigger, sometimes
            goto redo;

          default:
            panic ("REB_R value not supported for path dispatch");
        }
    }

    // A function being refined does not actually update pvs->out with
    // a "more refined" function value, it holds the original function and
    // accumulates refinement state on the stack.  The label should only
    // be captured the first time the function is seen, otherwise it would
    // capture the last refinement's name, so check label for non-NULL.
    //
    if (IS_ACTION(pvs->out) and IS_WORD(PVS_PICKER(pvs))) {
        if (not pvs->opt_label)
            pvs->opt_label = VAL_WORD_SPELLING(PVS_PICKER(pvs));
    }

    if (IS_END(pvs->value))
        return false; // did not throw

    return Next_Path_Throws(pvs);
}


//
//  Eval_Path_Throws_Core: C
//
// Evaluate an ANY_PATH! REBVAL, starting from the index position of that
// path value and continuing to the end.
//
// The evaluator may throw because GROUP! is evaluated, e.g. `foo/(throw 1020)`
//
// If label_sym is passed in as being non-null, then the caller is implying
// readiness to process a path which may be a function with refinements.
// These refinements will be left in order on the data stack in the case
// that `out` comes back as IS_ACTION().  If it is NULL then a new ACTION!
// will be allocated, in the style of the REFINE native, which will have the
// behavior of refinement partial specialization.
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
bool Eval_Path_Throws_Core(
    REBVAL *out, // if opt_setval, this is only used to return a thrown value
    REBSTR **label_out,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    const REBVAL *opt_setval, // Note: may be the same as out!
    REBFLGS flags
){
    assert(index == 0); // !!! current rule, immutable proxy w/AS may relax it

    if (flags & EVAL_FLAG_SET_PATH_ENFIXED)
        assert(opt_setval); // doesn't make any sense for GET-PATH! or PATH!

    // Treat a 0-length PATH! as if it gives back an ACTION! which does "what
    // a zero length path would do", e.g. an analogue to division (though in
    // the future, types might define this some other way.)
    //
    if (IS_END(ARR_AT(array, index))) {
        if (label_out)
            *label_out = nullptr;
        Move_Value(out, NAT_VALUE(path_0));
        return false;
    }

    // Paths that start with inert values do not evaluate.  So `/foo/bar` has
    // a REFINEMENT! at its head, and it will just be inert.  This also
    // means that `/foo/1` is inert, as opposed to #"o".  Note that this
    // is different from `(/foo)/1` or `ref: /foo | ref/1`, both of which
    // would be #"o".
    //
    if (ANY_INERT(ARR_AT(array, index))) {
        if (opt_setval)
            fail ("Can't perform SET_PATH! on path with inert head");
        Init_Any_Path_At_Core(out, REB_PATH, array, index, nullptr);
        return false;
    }

    DECLARE_FRAME (pvs);

    Push_Frame_At(pvs, array, index, specifier, flags | EVAL_FLAG_PATH_MODE);
    assert(NOT_END(pvs->value)); // tested 0-length path previously

    // Push_Frame_At sets the output to the global unwritable END cell, so we
    // have to wait for this point to set to the output cell we want.
    //
    pvs->out = out;
    SET_END(out);

    REBDSP dsp_orig = DSP;

    assert(
        not opt_setval
        or not IN_DATA_STACK_DEBUG(opt_setval) // evaluation might relocate it
    );
    assert(out != opt_setval and out != PVS_PICKER(pvs));

    pvs->special = opt_setval; // a.k.a. PVS_OPT_SETVAL()
    assert(PVS_OPT_SETVAL(pvs) == opt_setval);

    pvs->opt_label = NULL;

    // Seed the path evaluation process by looking up the first item (to
    // get a datatype to dispatch on for the later path items)
    //
    if (IS_WORD(pvs->value)) {
        //
        // Remember the actual location of this variable, not just its value,
        // in case we need to do R_IMMEDIATE writeback (e.g. month/day: 1)
        //
        pvs->u.ref.cell = Get_Mutable_Var_May_Fail(pvs->value, pvs->specifier);

        Move_Value(pvs->out, KNOWN(pvs->u.ref.cell));

        if (IS_ACTION(pvs->out)) {
            if (GET_CELL_FLAG(pvs->u.ref.cell, ENFIXED))
                SET_CELL_FLAG(pvs->out, ENFIXED);

            pvs->opt_label = VAL_WORD_SPELLING(pvs->value);
        }
    }
    else if (
        IS_GROUP(pvs->value)
        and NOT_EVAL_FLAG(pvs, PATH_HARD_QUOTE) // not precomposed
    ){
        pvs->u.ref.cell = nullptr; // nowhere to R_IMMEDIATE write back to

        if (GET_EVAL_FLAG(pvs, NO_PATH_GROUPS))
            fail ("GROUP! in PATH! used with GET or SET (use REDUCE/EVAL)");

        REBSPC *derived = Derive_Specifier(pvs->specifier, pvs->value);
        if (Do_At_Throws(
            pvs->out,
            VAL_ARRAY(pvs->value),
            VAL_INDEX(pvs->value),
            derived
        )){
            goto return_thrown;
        }
    }
    else {
        pvs->u.ref.cell = nullptr; // nowhere to R_IMMEDIATE write back to

        Derelativize(pvs->out, pvs->value, pvs->specifier);
    }

    if (IS_NULLED(pvs->out))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    Fetch_Next_In_Frame(nullptr, pvs);

    if (IS_END(pvs->value)) {
        // If it was a single element path, return the value rather than
        // try to dispatch it (would cause a crash at time of writing)
        //
        // !!! Is this the desired behavior, or should it be an error?
    }
    else {
        if (Next_Path_Throws(pvs))
            goto return_thrown;

        assert(IS_END(pvs->value));
    }

    if (opt_setval) {
        // If SET then we don't return anything
        goto return_not_thrown;
    }

    if (dsp_orig != DSP) {
        //
        // To make things easier for processing, reverse any refinements
        // pushed as ISSUE!s (we needed to evaluate them in forward order).
        // This way we can just pop them as we go, and know if they weren't
        // all consumed if not back to `dsp_orig` by the end.

        REBVAL *bottom = DS_AT(dsp_orig + 1);
        REBVAL *top = DS_TOP;

        while (top > bottom) {
            assert(IS_ISSUE(bottom) and not IS_WORD_BOUND(bottom));
            assert(IS_ISSUE(top) and not IS_WORD_BOUND(top));

            // It's faster to just swap the spellings.  (If binding
            // mattered, we'd need to swap the whole cells).
            //
            REBSTR *temp = PAYLOAD(Word, bottom).spelling;
            PAYLOAD(Word, bottom).spelling = PAYLOAD(Word, top).spelling;
            PAYLOAD(Word, top).spelling = temp;

            top--;
            bottom++;
        }

        assert(IS_ACTION(pvs->out));

        if (GET_EVAL_FLAG(pvs, PUSH_PATH_REFINEMENTS)) {
            //
            // The caller knows how to handle the refinements-pushed-to-stack
            // in-reverse-order protocol, and doesn't want to pay for making
            // a new ACTION!.
        }
        else {
            // The caller actually wants an ACTION! value to store or use
            // for later, as opposed to just calling it once.  It costs a
            // bit to do this, but unlike in R3-Alpha, it's possible to do!
            //
            // Code for specialization via refinement order works from the
            // data stack.  (It can't use direct value pointers because it
            // pushes to the stack itself, hence may move it on expansion.)
            //
            if (Specialize_Action_Throws(
                PVS_PICKER(pvs),
                pvs->out,
                pvs->opt_label,
                NULL, // opt_def
                dsp_orig // first_refine_dsp
            )){
                panic ("REFINE-only specializations should not THROW");
            }

            Move_Value(pvs->out, PVS_PICKER(pvs));
        }
    }

  return_not_thrown:;
    if (label_out)
        *label_out = pvs->opt_label;

    Abort_Frame(pvs);
    assert(not Is_Evaluator_Throwing_Debug());
    return false; // not thrown

  return_thrown:;
    Abort_Frame(pvs);
    assert(Is_Evaluator_Throwing_Debug());
    return true; // thrown
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
    if (IS_WORD(val) or IS_GET_WORD(val))
        Move_Opt_Var_May_Fail(out, val, specifier);
    else if (IS_PATH(val) or IS_GET_PATH(val))
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
// !!! This routine overlaps the logic of Eval_Path, and should potentially
// be a mode of that instead.  It is not very complete, considering that it
// does not execute GROUP! (and perhaps shouldn't?) and only supports a
// path that picks contexts out of other contexts, via word selection.
//
REBCTX *Resolve_Path(const REBVAL *path, REBCNT *index_out)
{
    REBARR *array = VAL_ARRAY(path);
    RELVAL *picker = ARR_HEAD(array);

    if (IS_END(picker) or not ANY_WORD(picker))
        return NULL; // !!! only handles heads of paths that are ANY-WORD!

    const RELVAL *var = Get_Opt_Var_May_Fail(picker, VAL_SPECIFIER(path));

    ++picker;
    if (IS_END(picker))
        return NULL; // !!! does not handle single-element paths

    while (ANY_CONTEXT(var) and IS_WORD(picker)) {
        REBCNT i = Find_Canon_In_Context(
            VAL_CONTEXT(var), VAL_WORD_CANON(picker), false
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
//  pick: native [
//
//  {Perform a path picking operation, same as `:(:location)/(:picker)`}
//
//      return: [<opt> any-value!]
//          {Picked value, or null if picker can't fulfill the request}
//      location [any-value!]
//      picker [any-value!]
//          {Index offset, symbol, or other value to use as index}
//  ]
//
REBNATIVE(pick)
//
// In R3-Alpha, PICK was an "action", which dispatched on types through the
// "action mechanic" for the following types:
//
//     [any-series! map! gob! pair! date! time! tuple! bitset! port! varargs!]
//
// In Ren-C, PICK is rethought to use the same dispatch mechanic as paths,
// to cut down on the total number of operations the system has to define.
{
    INCLUDE_PARAMS_OF_PICK;

    REBVAL *location = ARG(location);

    // PORT!s are kind of a "user defined type" which historically could
    // react to PICK and POKE, but which could not override path dispatch.
    // Use a symbol-based call to bounce the frame to the port, which should
    // be a compatible frame with the historical "action".
    //
    if (IS_PORT(location)) {
        DECLARE_LOCAL (word);
        Init_Word(word, Canon(SYM_PICK));
        return Do_Port_Action(frame_, location, word);
    }

    DECLARE_FRAME (pvs);
    pvs->flags.bits = DO_MASK_DEFAULT;

    Move_Value(D_OUT, location);
    pvs->out = D_OUT;

    Move_Value(PVS_PICKER(pvs), ARG(picker));

    pvs->value = END_NODE;
    pvs->specifier = SPECIFIED;

    pvs->opt_label = NULL; // applies to e.g. :append/only returning APPEND
    pvs->special = NULL;

  redo:;
    enum Reb_Kind kind = VAL_TYPE(D_OUT);
    PATH_HOOK hook = Path_Hooks[kind];

    REB_R r = hook(pvs, PVS_PICKER(pvs), NULL);
    if (not r or r == pvs->out or GET_CELL_FLAG(r, ROOT))
        return r;

    switch (KIND_BYTE(r)) {
      case REB_0_END:
        assert(r == R_UNHANDLED);
        fail (Error_Bad_Path_Pick_Raw(PVS_PICKER(pvs)));

      case REB_R_INVISIBLE:
        assert(false); // only SETs should do this
        break;

      case REB_R_REFERENCE: {
        assert(pvs->out == D_OUT);
        bool was_const = GET_CELL_FLAG(D_OUT, CONST);
        Derelativize(
            D_OUT,
            pvs->u.ref.cell,
            pvs->u.ref.specifier
        );
        if (was_const) // can't Inherit_Const(), flag would be overwritten
            SET_CELL_FLAG(D_OUT, CONST);
        return D_OUT; }

      case REB_R_REDO:
        goto redo;

      default:
        panic ("Unsupported return value in Path Dispatcher");
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
    if (IS_PORT(location)) {
        DECLARE_LOCAL (word);
        Init_Word(word, Canon(SYM_POKE));
        return Do_Port_Action(frame_, location, word);
    }

    DECLARE_FRAME (pvs);
    pvs->flags.bits = DO_MASK_DEFAULT;

    Move_Value(D_OUT, location);
    pvs->out = D_OUT;

    Move_Value(PVS_PICKER(pvs), ARG(picker));

    pvs->value = END_NODE;
    pvs->specifier = SPECIFIED;

    pvs->opt_label = NULL; // applies to e.g. :append/only returning APPEND
    pvs->special = ARG(value);

    enum Reb_Kind kind = VAL_TYPE(location);
    PATH_HOOK hook = Path_Hooks[kind];

    const REBVAL *r = hook(pvs, PVS_PICKER(pvs), ARG(value));
    switch (KIND_BYTE(r)) {
    case REB_0_END:
        assert(r == R_UNHANDLED);
        fail (Error_Bad_Path_Poke_Raw(PVS_PICKER(pvs)));

    case REB_R_INVISIBLE: // is saying it did the write already
        break;

    case REB_R_REFERENCE: // wants us to write it
        Move_Value(pvs->u.ref.cell, ARG(value));
        break;

    default:
        assert(false); // shouldn't happen, complain in the debug build
        fail (PVS_PICKER(pvs)); // raise error in release build
    }

    RETURN (ARG(value)); // return the value we got in
}


//
//  path-0: enfix native [
//
//  {Temporary native in lieu of PD_Xxx() dispatch so `/` performs division}
//
//      left [<opt> any-value!]
//      right [<opt> any-value!]
//  ]
//
REBNATIVE(path_0)
{
    INCLUDE_PARAMS_OF_PATH_0;

    REBVAL *left = ARG(left);
    REBVAL *right = ARG(right);

    // !!! Somewhat whimsically, this goes ahead and guesses at a possible
    // behavior for "dividing" strings using SPLIT.  This is a placeholder
    // for the idea that the left hand type gets to dispatch a choice of
    // what it means, as with ordinary path dispatch.
    //
    // Uses the /INTO refinement so that `"abcdef" / 2` divides the string
    // into two pieces, as opposed to pieces of length 2.
    //
    if (ANY_STRING(left) or ANY_ARRAY(left))
        return rebRun("split/into", left, right, rebEND);

    // Note: DIVIDE is historically a "type action", so technically it is the
    // left hand side type which gets to pick the behavior--consistent with
    // the plan for how 0-length paths would work.
    //
    return rebRun("divide", left, right, rebEND);
}


//
//  PD_Path: C
//
// A PATH! is not an array, but if it is implemented as one it may choose to
// dispatch path handling to its array.
//
REB_R PD_Path(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    if (opt_setval)
        fail ("PATH!s are immutable (convert to GROUP! or BLOCK! to mutate)");

    return PD_Array(pvs, picker, opt_setval);
}


//
//  REBTYPE: C
//
// The concept of PATH! is now that it is an immediate value.  While it
// permits picking and enumeration, it may or may not have an actual REBARR*
// node backing it.
//
// !!! Changing the workings of path is experimental...but it is believed that
// the old model for PATH! as isomorphic to GROUP! and BLOCK! was flawed.
//
REBTYPE(Path)
{
    REBVAL *path = D_ARG(1);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));

        switch (VAL_WORD_SYM(ARG(property))) {
          case SYM_LENGTH:
            return Series_Common_Action_Maybe_Unhandled(frame_, verb);

          // !!! Any other interesting reflectors?

          case SYM_INDEX: // not legal, paths always at head, no index
          default:
            break;
        }
        break; }

        // Since ANY-PATH! is immutable, a shallow copy should be cheap, but
        // it should be cheap for any similarly marked array.  Also, a /DEEP
        // copy of a path may copy groups that are mutable.
        //
      case SYM_COPY:
        goto retrigger;

      default:
        break;
    }

    fail (Error_Illegal_Action(VAL_TYPE(path), verb));

  retrigger:;

    return T_Array(frame_, verb);
}


//
//  MF_Path: C
//
void MF_Path(REB_MOLD *mo, const REBCEL *v, bool form)
{
    UNUSED(form);

    REBARR *a = VAL_ARRAY(v);

    // Recursion check:
    if (Find_Pointer_In_Series(TG_Mold_Stack, a) != NOT_FOUND) {
        Append_Unencoded(mo->series, ".../...");
        return;
    }
    Push_Pointer_To_Series(TG_Mold_Stack, a);

    // Routine may be called on value that reports REB_QUOTED, even if it
    // has no additional payload and is aliasing the cell itself.  Checking
    // the type could be avoided if each type had its own dispatcher, but
    // this routine seems to need to be generic.
    //
    enum Reb_Kind kind = CELL_KIND(v);

    if (kind == REB_GET_PATH)
        Append_Utf8_Codepoint(mo->series, ':');

    assert(VAL_INDEX(v) == 0); // the new rule, not an ANY-ARRAY!, always head
    assert(ARR_LEN(a) >= 2); // another new rule, even / is `make path! [_ _]`

    RELVAL *item = ARR_HEAD(a);
    while (NOT_END(item)) {
        assert(not ANY_PATH(item)); // another new rule

        if (not IS_BLANK(item)) { // no blank molding; indicated by slashes
            //
            // !!! Molding of items in paths which have slashes in them, such
            // as URL! or FILE! (or some historical date formats) need some
            // kind of escaping, otherwise they have to be outlawed too.
            // FILE! has the option of `a/%"dir/file.txt"/b` to put the file
            // in quotes, but URL does not.
            //
            Mold_Value(mo, item);

            // Note: We ignore VALUE_FLAG_NEWLINE_BEFORE here for ANY-PATH,
            // but any embedded BLOCK! or GROUP! which do have newlines in
            // them can make newlines, e.g.:
            //
            //     a/[
            //        b c d
            //     ]/e
        }

        ++item;
        if (IS_END(item))
            break;

        Append_Utf8_Codepoint(mo->series, '/');
    }

    if (kind == REB_SET_PATH)
        Append_Utf8_Codepoint(mo->series, ':');

    Drop_Pointer_From_Series(TG_Mold_Stack, a);
}


//
//  MAKE_Path: C
//
// A MAKE of a PATH! is experimentally being thought of as evaluative.  This
// is in line with the most popular historical interpretation of MAKE, for
// MAKE OBJECT!--which evaluates the object body block.
//
REB_R MAKE_Path(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    if (not IS_BLOCK(arg))
        fail (Error_Bad_Make(kind, arg)); // "make path! 0" has no meaning

    DECLARE_FRAME (f);
    Push_Frame(f, arg);

    REBDSP dsp_orig = DSP;

    while (NOT_END(f->value)) {
        if (Eval_Step_Throws(SET_END(out), f)) {
            Abort_Frame(f);
            return R_THROWN;
        }

        if (IS_END(out))
            break;
        if (IS_NULLED(out))
            continue;

        if (not ANY_PATH(out)) {
            if (DSP != dsp_orig and IS_BLANK(DS_TOP))
                DS_DROP(); // make path! ['a/ 'b] => a/b, not a//b
            Move_Value(DS_PUSH(), out);
        }
        else { // Splice any generated paths, so there are no paths-in-paths.

            RELVAL *item = VAL_ARRAY_AT(out);
            if (IS_BLANK(item) and DSP != dsp_orig) {
                if (IS_BLANK(DS_TOP)) // make path! ['a/b/ `/c`]
                    fail ("Cannot merge slashes in MAKE PATH!");
                ++item;
            }
            else if (DSP != dsp_orig and IS_BLANK(DS_TOP))
                DS_DROP(); // make path! ['a/ 'b/c] => a/b/c, not a//b/c

            for (; NOT_END(item); ++item)
                Derelativize(DS_PUSH(), item, VAL_SPECIFIER(out));
        }
    }

    REBARR *arr = Pop_Stack_Values_Core(dsp_orig, NODE_FLAG_MANAGED);
    Drop_Frame_Unbalanced(f); // !!! f->dsp_orig got captured each loop

    if (ARR_LEN(arr) < 2) // !!! Should pass produced array as BLOCK! to error
        fail ("MAKE PATH! must produce path of at least length 2");

    return Init_Any_Path(out, kind, arr);
}


static void Push_Path_Recurses(RELVAL *path, REBSPC *specifier)
{
    RELVAL *item = VAL_ARRAY_AT(path);
    for (; NOT_END(item); ++item) {
        if (IS_PATH(item))
            Push_Path_Recurses(item, Derive_Specifier(specifier, item));
        else
            Derelativize(DS_PUSH(), item, specifier);
    }
}


//
//  TO_Path: C
//
REB_R TO_Path(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    if (not ANY_ARRAY(arg))
        fail (Error_Bad_Make(kind, arg)); // "to path! 0" has no meaning

    REBDSP dsp_orig = DSP;
    RELVAL *item = VAL_ARRAY_AT(arg);
    for (; NOT_END(item); ++item) {
        if (IS_PATH(item))
            Push_Path_Recurses(item, VAL_SPECIFIER(arg));
        else
            Derelativize(DS_PUSH(), item, VAL_SPECIFIER(arg));
    }

    if (DSP - dsp_orig < 2)
        fail ("TO PATH! must produce a path of at least length 2");

    return Init_Any_Path(out, kind, Pop_Stack_Values(dsp_orig));
}


//
//  CT_Path: C
//
// "Compare Type" dispatcher for the following types: (list here to help
// text searches)
//
//     CT_Set_Path()
//     CT_Get_Path()
//     CT_Lit_Path()
//
REBINT CT_Path(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    REBINT num = Cmp_Array(a, b, mode == 1);
    if (mode >= 0)
        return (num == 0);
    if (mode == -1)
        return (num >= 0);
    return (num > 0);
}
