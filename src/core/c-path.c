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
// In order to avoid having to pay for a check for nullptr in the path dispatch
// table for types with no path dispatch, a failing handler is in the slot.
//
Bounce PD_Fail(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    UNUSED(pvs);
    UNUSED(picker);
    UNUSED(opt_setval);

    return BOUNCE_UNHANDLED;
}


//
//  PD_Unhooked: C
//
// As a temporary workaround for not having real user-defined types, an
// extension can overtake an "unhooked" type slot to provide behavior.
//
Bounce PD_Unhooked(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    UNUSED(pvs);
    UNUSED(picker);
    UNUSED(opt_setval);

    const Value* type = Datatype_From_Kind(Type_Of(pvs->out));
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
    if (Is_Nulled(pvs->out))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    PATH_HOOK hook = Path_Hooks[Type_Of(pvs->out)];
    assert(hook != nullptr);  // &PD_Fail is used instead of nullptr

    if (Is_Get_Word(pvs->value)) { // e.g. object/:field
        Move_Opt_Var_May_Fail(PVS_PICKER(pvs), pvs->value, pvs->specifier);
    }
    else if (Is_Group(pvs->value)) { // object/(expr) case:
        if (pvs->flags.bits & DO_FLAG_NO_PATH_GROUPS)
            fail ("GROUP! in PATH! used with GET or SET (use REDUCE/EVAL)");

        Specifier* derived = Derive_Specifier(pvs->specifier, pvs->value);
        if (Eval_Array_At_Throws(
            PVS_PICKER(pvs),
            Cell_Array(pvs->value),
            VAL_INDEX(pvs->value),
            derived
        )) {
            Copy_Cell(pvs->out, PVS_PICKER(pvs));
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
    if (Is_Nulled(PVS_PICKER(pvs)))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    Fetch_Next_In_Level(nullptr, pvs); // may be at end

    if (IS_END(pvs->value) and PVS_IS_SET_PATH(pvs)) {
        const Value* bounce = hook(
            pvs,
            PVS_PICKER(pvs),
            PVS_OPT_SETVAL(pvs)
        );

        if (bounce == nullptr)
            fail (Error_Bad_Path_Poke_Raw(PVS_PICKER(pvs)));

        switch (VAL_TYPE_RAW(bounce)) {

        case TYPE_0_END: // unhandled
            assert(bounce == BOUNCE_UNHANDLED); // shouldn't be other ends
            fail (Error_Bad_Path_Poke_Raw(PVS_PICKER(pvs)));

        case TYPE_R_THROWN:
            panic ("Path dispatch isn't allowed to throw, only GROUP!s");

        case TYPE_R_INVISIBLE: // dispatcher assigned target with opt_setval
            break; // nothing left to do, have to take the dispatcher's word

        case TYPE_R_REFERENCE: { // dispatcher wants a set *if* at end of path
            Copy_Cell(pvs->u.ref.cell, PVS_OPT_SETVAL(pvs));
            break; }

        case TYPE_R_IMMEDIATE: {
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
            // would be the Varlist_Slot() where month was found initially, and so
            // we write the updated bits from pvs->out there.

            if (not pvs->u.ref.cell)
                fail ("Can't update temporary immediate value via SET-PATH!");

            Copy_Cell(pvs->u.ref.cell, pvs->out);
            break; }

        default:
            //
            // Something like a generic OUT.  We could in theory take those
            // to just be variations of BOUNCE_IMMEDIATE, but it's safer to break
            // that out as a separate class.
            //
            fail ("Path evaluation produced temporary value, can't POKE it");
        }
        Corrupt_Pointer_If_Debug(pvs->special);
    }
    else {
        pvs->u.ref.cell = nullptr; // clear status of the reference

        const Value* r = hook(
            pvs,
            PVS_PICKER(pvs),
            nullptr // no opt_setval, GET-PATH! or a SET-PATH! not at the end
        );

        if (r and r != END_NODE) {
            assert(r->header.bits & NODE_FLAG_CELL);
            /* assert(not (r->header.bits & NODE_FLAG_ROOT)); */
        }

        if (r == pvs->out) {
            assert(not THROWN(pvs->out));
        }
        else if (not r) {
            Init_Nulled(pvs->out);
        }
        else if (r == BOUNCE_UNHANDLED) {
            if (Is_Nulled(PVS_PICKER(pvs)))
                fail ("NULL used in path picking but was not handled");
            fail (Error_Bad_Path_Pick_Raw(PVS_PICKER(pvs)));
        }
        else if (VAL_TYPE_RAW(r) <= TYPE_NULLED) {
            Handle_Api_Dispatcher_Result(pvs, r);
        }
        else switch (VAL_TYPE_RAW(r)) {

        case TYPE_0_END:
            fail (Error_Bad_Path_Pick_Raw(PVS_PICKER(pvs)));

        case TYPE_R_THROWN:
            panic ("Path dispatch isn't allowed to throw, only GROUP!s");

        case TYPE_R_INVISIBLE:
            panic("SET-PATH! evaluation ran assignment before path end");

        case TYPE_R_REFERENCE:
            Derelativize(
                pvs->out,
                pvs->u.ref.cell,
                pvs->u.ref.specifier
            );
            if (Get_Cell_Flag(pvs->u.ref.cell, INFIX_IF_ACTION))
                Set_Cell_Flag(pvs->out, INFIX_IF_ACTION);

            // Leave the pvs->u.ref as-is in case the next update turns out
            // to be BOUNCE_IMMEDIATE, and it is needed.
            break;

        default:
            panic ("Bounce value not supported for path dispatch");
        }
    }

    // A function being refined does not actually update pvs->out with
    // a "more refined" function value, it holds the original function and
    // accumulates refinement state on the stack.  The label should only
    // be captured the first time the function is seen, otherwise it would
    // capture the last refinement's name, so check label for non-nullptr.
    //
    if (Is_Action(pvs->out) and Is_Word(PVS_PICKER(pvs))) {
        if (not pvs->opt_label)
            pvs->opt_label = Cell_Word_Symbol(PVS_PICKER(pvs));
    }

    if (
        IS_END(pvs->value)
        or (Is_Blank(pvs->value) and IS_END(pvs->value + 1))
    ){
        return false; // did not throw
    }

    return Next_Path_Throws(pvs);
}


//
//  Eval_Path_Throws_Core: C
//
// Evaluate an Any_Path! Value, starting from the index position of that
// path value and continuing to the end.
//
// The evaluator may throw because GROUP! is evaluated, e.g. `foo/(throw 1020)`
//
// If label_sym is passed in as being non-null, then the caller is implying
// readiness to process a path which may be a function with refinements.
// These refinements will be left in order on the data stack in the case
// that `out` comes back as Is_Action().  If it is nullptr then a new ACTION!
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
    Value* out, // if opt_setval, this is only used to return a thrown value
    Symbol* *label_out,
    Array* array,
    REBLEN index,
    Specifier* specifier,
    const Value* opt_setval, // Note: may be the same as out!
    Flags flags
){
    // Treat a 0-length PATH! as if it gives back an ACTION! which does "what
    // a zero length path would do", e.g. an analogue to division (though in
    // the future, types might define this some other way.)
    //
    if (IS_END(Array_At(array, index))) {
        if (label_out)
            *label_out = nullptr;
        Copy_Cell(out, NAT_VALUE(PATH_0));
        return false;
    }

    // Paths that start with inert values do not evaluate.  So `/foo/bar` has
    // a REFINEMENT! at its head, and it will just be inert.  This also
    // means that `/foo/1` is inert, as opposed to #"o".  Note that this
    // is different from `(/foo)/1` or `ref: /foo | ref/1`, both of which
    // would be #"o".
    //
    if (Any_Inert(Array_At(array, index))) {
        if (opt_setval)
            fail ("Can't perform SET_PATH! on path with inert head");
        Init_Any_List_At(out, TYPE_PATH, array, index);
        return false;
    }

    DECLARE_LEVEL (pvs);

    Push_Level_At(pvs, array, index, specifier, flags);
    assert(NOT_END(pvs->value)); // tested 0-length path previously

    // Push_Level_At sets the output to the global unwritable END cell, so we
    // have to wait for this point to set to the output cell we want.
    //
    pvs->out = out;
    SET_END(out);

    StackIndex base = TOP_INDEX;

    assert(
        not opt_setval
        or not IN_DATA_STACK_DEBUG(opt_setval) // evaluation might relocate it
    );
    assert(out != opt_setval and out != PVS_PICKER(pvs));

    pvs->special = opt_setval; // a.k.a. PVS_OPT_SETVAL()
    assert(PVS_OPT_SETVAL(pvs) == opt_setval);

    pvs->opt_label = nullptr;

    // Seed the path evaluation process by looking up the first item (to
    // get a datatype to dispatch on for the later path items)
    //
    if (Is_Word(pvs->value)) {
        //
        // Remember the actual location of this variable, not just its value,
        // in case we need to do BOUNCE_IMMEDIATE writeback (e.g. month/day: 1)
        //
        pvs->u.ref.cell = Get_Mutable_Var_May_Fail(pvs->value, pvs->specifier);

        Copy_Cell(pvs->out, KNOWN(pvs->u.ref.cell));

        if (Is_Action(pvs->out)) {
            if (Get_Cell_Flag(pvs->u.ref.cell, INFIX_IF_ACTION))
                Set_Cell_Flag(pvs->out, INFIX_IF_ACTION);

            pvs->opt_label = Cell_Word_Symbol(pvs->value);
        }
    }
    else if (Is_Group(pvs->value)) {
        pvs->u.ref.cell = nullptr; // nowhere to BOUNCE_IMMEDIATE write back to

        if (pvs->flags.bits & DO_FLAG_NO_PATH_GROUPS)
            fail ("GROUP! in PATH! used with GET or SET (use REDUCE/EVAL)");

        Specifier* derived = Derive_Specifier(pvs->specifier, pvs->value);
        if (Eval_Array_At_Throws(
            pvs->out,
            Cell_Array(pvs->value),
            VAL_INDEX(pvs->value),
            derived
        )){
            goto return_thrown;
        }
    }
    else {
        pvs->u.ref.cell = nullptr; // nowhere to BOUNCE_IMMEDIATE write back to

        Derelativize(pvs->out, pvs->value, pvs->specifier);
    }

    if (Is_Nulled(pvs->out))
        fail (Error_No_Value_Core(pvs->value, pvs->specifier));

    Fetch_Next_In_Level(nullptr, pvs);

    assert(NOT_END(pvs->value));  // paths must be at least 2 long
    if (Is_Blank(pvs->value) and IS_END(pvs->value + 1)) {
        //
        // Paths like `append/` will get the value and ensure it is an
        // action.  so `x: 10 x/` should fail.  !!! Fail in callers or here?
    }
    else {
        if (Next_Path_Throws(pvs))
            goto return_thrown;

        assert(
            IS_END(pvs->value)
            or (Is_Blank(pvs->value) and IS_END(pvs->value + 1))
        );
    }

    if (opt_setval) {
        // If SET then we don't return anything
        goto return_not_thrown;
    }

    if (base != TOP_INDEX) {
        //
        // To make things easier for processing, reverse any refinements
        // pushed as ISSUE!s (we needed to evaluate them in forward order).
        // This way we can just pop them as we go, and know if they weren't
        // all consumed if not back to `base` by the end.

        Value* bottom = Data_Stack_At(base + 1);
        Value* top = TOP;

        while (top > bottom) {
            assert(Is_Issue(bottom) and not IS_WORD_BOUND(bottom));
            assert(Is_Issue(top) and not IS_WORD_BOUND(top));

            // It's faster to just swap the spellings.  (If binding
            // mattered, we'd need to swap the whole cells).
            //
            Symbol* temp = bottom->payload.any_word.symbol;
            bottom->payload.any_word.symbol
                = top->payload.any_word.symbol;
            top->payload.any_word.symbol = temp;

            top--;
            bottom++;
        }

        assert(Is_Action(pvs->out));

        if (pvs->flags.bits & DO_FLAG_PUSH_PATH_REFINEMENTS) {
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
                nullptr,  // opt_def
                base // first_refine_dsp
            )){
                panic ("REFINE-only specializations should not THROW");
            }

            Copy_Cell(pvs->out, PVS_PICKER(pvs));
        }
    }

  return_not_thrown:;
    if (label_out)
        *label_out = pvs->opt_label;

    Abort_Level(pvs);
    assert(not THROWN(out));
    return false;

  return_thrown:;
    Abort_Level(pvs);
    assert(THROWN(out));
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
void Get_Simple_Value_Into(Value* out, const Cell* val, Specifier* specifier)
{
    if (Is_Word(val) or Is_Get_Word(val))
        Move_Opt_Var_May_Fail(out, val, specifier);
    else if (Is_Path(val) or Is_Get_Path(val))
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
VarList* Resolve_Path(const Value* path, REBLEN *index_out)
{
    Array* array = Cell_Array(path);
    Cell* picker = Array_Head(array);

    if (IS_END(picker) or not Any_Word(picker))
        return nullptr;  // !!! only handles heads of paths that are ANY-WORD!

    const Cell* var = Get_Opt_Var_May_Fail(picker, VAL_SPECIFIER(path));

    ++picker;
    if (IS_END(picker))
        return nullptr;  // !!! does not handle single-element paths

    while (Any_Context(var) and Is_Word(picker)) {
        REBLEN i = Find_Canon_In_Context(
            Cell_Varlist(var), VAL_WORD_CANON(picker), false
        );
        ++picker;
        if (IS_END(picker)) {
            *index_out = i;
            return Cell_Varlist(var);
        }

        var = Varlist_Slot(Cell_Varlist(var), i);
    }

    return nullptr;
}


//
//  pick: native [
//
//  {Perform a path picking operation, same as `:(:location)/(:picker)`}
//
//      return: [any-value!]
//          {Picked value, or null if picker can't fulfill the request}
//      location [any-value!]
//      picker [any-value!]
//          {Index offset, symbol, or other value to use as index}
//  ]
//
DECLARE_NATIVE(PICK)
//
// In R3-Alpha, PICK was an "action", which dispatched on types through the
// "action mechanic" for the following types:
//
//     [any-series! map! pair! date! time! tuple! bitset! port! varargs!]
//
// In Ren-C, PICK is rethought to use the same dispatch mechanic as paths,
// to cut down on the total number of operations the system has to define.
{
    INCLUDE_PARAMS_OF_PICK;

    Value* location = ARG(LOCATION);

    // PORT!s are kind of a "user defined type" which historically could
    // react to PICK and POKE, but which could not override path dispatch.
    // Use a symbol-based call to bounce the frame to the port, which should
    // be a compatible frame with the historical "action".
    //
    if (Is_Port(location)) {
        DECLARE_VALUE (word);
        Init_Word(word, CANON(PICK));
        return Do_Port_Action(level_, location, word);
    }

    DECLARE_LEVEL (pvs);
    pvs->flags = Endlike_Header(DO_MASK_NONE);

    Copy_Cell(OUT, location);
    pvs->out = OUT;

    Copy_Cell(PVS_PICKER(pvs), ARG(PICKER));

    pvs->value = END_NODE;
    pvs->specifier = SPECIFIED;

    pvs->opt_label = nullptr;  // applies to e.g. :append/only returning APPEND
    pvs->special = nullptr;

    PATH_HOOK hook = Path_Hooks[Type_Of(location)];
    assert(hook != nullptr); // &PD_Fail is used instead of null

    Bounce bounce = hook(pvs, PVS_PICKER(pvs), nullptr);
    if (not bounce)
        return bounce;

    switch (VAL_TYPE_RAW(bounce)) {
    case TYPE_0_END:
        assert(bounce == BOUNCE_UNHANDLED);
        fail (Error_Bad_Path_Pick_Raw(PVS_PICKER(pvs)));

    case TYPE_R_INVISIBLE:
        assert(false); // only SETs should do this
        break;

    case TYPE_R_REFERENCE:
        Derelativize(
            OUT,
            pvs->u.ref.cell,
            pvs->u.ref.specifier
        );
        return OUT;

    default:
        break;
    }

    return bounce;
}


//
//  poke: native [
//
//  {Perform a path poking operation, same as `(:location)/(:picker): :value`}
//
//      return: [any-value!]
//          {Same as value}
//      location [any-value!]
//          {(modified)}
//      picker
//          {Index offset, symbol, or other value to use as index}
//      value [any-value! trash!]
//          {The new value}
//  ]
//
DECLARE_NATIVE(POKE)
//
// As with PICK*, POKE is changed in Ren-C from its own action to "whatever
// path-setting (now path-poking) would do".
{
    INCLUDE_PARAMS_OF_POKE;

    Value* location = ARG(LOCATION);

    // PORT!s are kind of a "user defined type" which historically could
    // react to PICK and POKE, but which could not override path dispatch.
    // Use a symbol-based call to bounce the frame to the port, which should
    // be a compatible frame with the historical "action".
    //
    if (Is_Port(location)) {
        DECLARE_VALUE (word);
        Init_Word(word, CANON(POKE));
        return Do_Port_Action(level_, location, word);
    }

    DECLARE_LEVEL (pvs);
    pvs->flags = Endlike_Header(DO_MASK_NONE);

    Copy_Cell(OUT, location);
    pvs->out = OUT;

    Copy_Cell(PVS_PICKER(pvs), ARG(PICKER));

    pvs->value = END_NODE;
    pvs->specifier = SPECIFIED;

    pvs->opt_label = nullptr;  // applies to e.g. :append/only returning APPEND
    pvs->special = ARG(VALUE);

    PATH_HOOK hook = Path_Hooks[Type_Of(location)];
    assert(hook); // &PD_Fail is used instead of nullptr

    const Value* bounce = hook(pvs, PVS_PICKER(pvs), ARG(VALUE));
    switch (VAL_TYPE_RAW(bounce)) {
      case TYPE_0_END:
        assert(bounce == BOUNCE_UNHANDLED);
        fail (Error_Bad_Path_Poke_Raw(PVS_PICKER(pvs)));

      case TYPE_R_INVISIBLE: // is saying it did the write already
        break;

      case TYPE_R_REFERENCE: // wants us to write it
        Copy_Cell(pvs->u.ref.cell, ARG(VALUE));
        break;

      default:
        assert(false); // shouldn't happen, complain in the debug build
        fail (Error_Invalid(PVS_PICKER(pvs))); // raise error in release build
    }

    RETURN (ARG(VALUE)); // return the value we got in
}


//
//  path-0: infix native [
//
//  {Temporary native in lieu of PD_Xxx() dispatch so `/` performs division}
//
//      left [any-value!]
//      right [any-value!]
//  ]
//
DECLARE_NATIVE(PATH_0)
{
    INCLUDE_PARAMS_OF_PATH_0;

    Value* left = ARG(LEFT);
    Value* right = ARG(RIGHT);

    // !!! Somewhat whimsically, this goes ahead and guesses at a possible
    // behavior for "dividing" strings using SPLIT.  This is a placeholder
    // for the idea that the left hand type gets to dispatch a choice of
    // what it means, as with ordinary path dispatch.
    //
    // Uses the /INTO refinement so that `"abcdef" / 2` divides the string
    // into two pieces, as opposed to pieces of length 2.
    //
    if (Any_String(left) or Any_List(left))
        return rebValue("split/into", left, right);

    // Note: DIVIDE is historically a "type action", so technically it is the
    // left hand side type which gets to pick the behavior--consistent with
    // the plan for how 0-length paths would work.
    //
    return rebValue("divide", left, right);
}
