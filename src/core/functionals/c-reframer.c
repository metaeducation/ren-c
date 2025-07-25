//
//  file: %c-reframer.c
//  summary: "Function that can transform arbitrary callsite functions"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// REFRAMER allows one to define a function that does generalized transforms
// on the input (and output) of other functions.  Unlike ENCLOSE, it does not
// specify an exact function it does surgery on the frame of ahead of time.
// Instead, each invocation of the reframing action interacts with the
// instance that follows it at the callsite.
//
// A simple example is a function which removes quotes from the first
// parameter to a function, and adds them back for the result:
//
//     requote: reframer func [f [frame!]] [
//         p: first words of f
//         num-quotes: quotes of f.(p)
//
//         f.(p): noquote f.(p)
//
//         return quote:depth eval f num-quotes
//     ]
//
//     >> item: the '''[a b c]
//     == '''[a b c]
//
//     >> requote append item <d>  ; append doesn't accept QUOTED? series
//     == '''[a b c <d>]   ; munging frame and result makes it seem to
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Infix handling is not yet implemented, e.g. `requote '''1 + 2`
//
// * Because reframers need to know the function they are operating on, they
//   are unable to "see through" a GROUP! to get it, as a group could contain
//   multiple expressions.  So `requote (append item <d>)` cannot work.
//
// * If you "reframe a reframer" at the moment, you will not likely get what
//   you want...as the arguments you want to inspect will be compacted into
//   a frame argument.  It may be possible to make a "compound frame" that
//   captures the user-perceived combination of a reframer and what it's
//   reframing, but that would be technically difficult.
//

#include "sys-core.h"

enum {
    IDX_REFRAMER_SHIM = 1,  // action that can manipulate the reframed frame
    IDX_REFRAMER_PARAM_INDEX,  // index in shim that receives FRAME!
    MAX_IDX_REFRAMER = IDX_REFRAMER_PARAM_INDEX
};


//
//  Make_Pushed_Level_From_Action_Feed_May_Throw: C
//
// 1. The idea of creating a level from an evaluative step which includes infix
//    as part of the step would ultimately have to make a composite level that
//    captured the entire chain of the operation.  That's a heavy concept, but
//    for now we just try to get multiple returns to work which are part of
//    the evaluator and hence can do trickier things.
//
// 2. At the moment, Begin_Action() marks the frame as having been invoked...
//
// 3. The function did not actually execute, so L->varlist was never handed
//    out...the varlist should never have gotten managed.  So this context
//    can theoretically just be put back into the reuse list, or managed
//    and handed out for other purposes.  Caller's choice.
//
Level* Make_Pushed_Level_From_Action_Feed_May_Throw(
    Sink(Value) out,
    Value* action,
    Feed* feed,
    StackIndex base,
    bool error_on_deferred
){
    require (
      Level* L = Make_Level(
        &Action_Executor,
        feed,
        LEVEL_MASK_NONE  // FULFILL_ONLY added after Push_Action()
    ));
    L->baseline.stack_base = base;  // incorporate refinements
    Push_Level_Erase_Out_If_State_0(u_cast(Atom*, out), L);

    if (error_on_deferred)  // can't deal with ELSE/THEN [1]
        L->flags.bits |= ACTION_EXECUTOR_FLAG_ERROR_ON_DEFERRED_INFIX;

    require (
      Push_Action(L, action, PREFIX_0)
    );

    ParamList* varlist = L->varlist;  // Drop_Action() will null out L->varlist

    Set_Executor_Flag(ACTION, L, FULFILL_ONLY);  // Push_Action() won't allow

    assert(Level_Coupling(L) == Frame_Coupling(action));  // no invocation

    if (Trampoline_With_Top_As_Root_Throws())
        return L;

    assert(Not_Base_Managed(varlist));  // shouldn't be [3]
    L->varlist = varlist;  // put varlist back

    assert(Is_Tripwire(Known_Stable(L->out)));  // only gathers arguments

    assert(Get_Flavor_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED));
    Clear_Flavor_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED);  // [2]

    L->u.action.original = Frame_Phase(action);
    Tweak_Level_Phase(L, Frame_Phase(action));  // Drop_Action() cleared
    Tweak_Level_Coupling(L, Frame_Coupling(action));

    return L;  // may not be at end or thrown, e.g. (/x: does+ just y x = 'y)
}


//
//  Init_Invokable_From_Feed: C
//
// This builds a frame from a feed *as if* it were going to be used to call
// an action, but doesn't actually make the call.  Instead it leaves the
// varlist available for other purposes.
//
// If the next item in the feed is not a WORD! or PATH! that look up to an
// action (nor an ACTION! literally) then the output will be set to a QUOTED?
// version of what would be evaluated to.  So in the case of NULL, it will be
// a single quote of nothing.
//
Result(Zero) Init_Invokable_From_Feed(
    Sink(Value) out,
    Option(const Element*) first,  // override first value, vs. At_Feed(feed)
    Feed* feed,
    bool error_on_deferred  // if not planning to keep running, can't ELSE/THEN
){
    const Element* v = first ? unwrap first : Try_At_Feed(feed);

    // Not all callers necessarily want to tolerate an end condition, so this
    // needs review.
    //
    if (v == nullptr)  // no first, and feed was at end
        return zero;

    if (Is_Group(v))  // `requote (append [a b c] #d, <can't-work>)`
        panic ("Actions made with REFRAMER cannot work with GROUP!s");

    StackIndex base = TOP_INDEX;

    if (Is_Word(v) or Is_Tuple(v) or Is_Path(v) or Is_Chain(v)) {
        require (
          Get_Var(out, NO_STEPS, v, Feed_Binding(feed))
        );
    }
    else
        Derelativize(out, v, Feed_Binding(feed));

    if (not first)  // nothing passed in, so we used a feed value
        Fetch_Next_In_Feed(feed);  // we've seen it now

    if (not Is_Action(out)) {
        Quotify(Known_Element(out));
        return zero;
    }

    // !!! Process_Action_Throws() calls Drop_Action() and loses the phase.
    // It probably shouldn't, but since it does we need the action afterward
    // to put the phase back.
    //
    DECLARE_VALUE (action);
    Move_Cell(action, out);
    Push_Lifeguard(action);

    Option(VarList*) coupling = Frame_Coupling(action);

    Level* L = Make_Pushed_Level_From_Action_Feed_May_Throw(
        out,
        action,
        feed,
        base,
        error_on_deferred
    );

    if (Is_Throwing(L)) {  // signals threw
        Drop_Level(L);
        Drop_Lifeguard(action);
        panic (Error_No_Catch_For_Throw(L));
    }

    // The exemplar may or may not be managed as of yet.  We want it
    // managed, but Push_Action() does not use ordinary series creation to
    // make its nodes, so manual ones don't wind up in the tracking list.
    //
    assert(Level_Coupling(L) == Frame_Coupling(action));

    assert(Not_Base_Managed(L->varlist));

    ParamList* varlist = cast(ParamList*, L->varlist);  // executor is nullptr
    L->varlist = nullptr;  // don't let Drop_Level() free varlist (we want it)
    Tweak_Misc_Runlevel(varlist, nullptr);  // disconnect from L
    Drop_Level(L);
    Drop_Lifeguard(action);

    Set_Base_Managed_Bit(varlist);  // can't use Manage_Stub

    ParamList* lens = Phase_Paramlist(Frame_Phase(action));
    Init_Lensed_Frame(out, varlist, lens, coupling);

    return zero;
}


//
//  Init_Frame_From_Feed: C
//
// Making an invokable from a feed might return a QUOTED?, because that is
// more efficient (and truthful) than creating a FRAME! for the identity
// function.  However, MAKE FRAME! of a VARARGS! was an experimental feature
// that has to follow the rules of MAKE FRAME!...e.g. returning a frame.
// This converts QUOTED?s into frames for the identity function.
//
Result(Zero) Init_Frame_From_Feed(
    Sink(Value) out,
    const Element* first,
    Feed* feed,
    bool error_on_deferred
){
    trap (
      Init_Invokable_From_Feed(out, first, feed, error_on_deferred)
    );
    if (Is_Frame(out))
        return zero;

    assert(Is_Quoted(out));
    ParamList* exemplar = Make_Varlist_For_Action(
        LIB(IDENTITY),
        TOP_INDEX,
        nullptr,
        nullptr  // leave unspecialized slots with parameter! antiforms
    );

    Value* var = Slot_Hack(Varlist_Slot(exemplar, 2));
    Unquotify(Copy_Cell(var, cast(Element*, out)));

    // Should we save the WORD! from a variable access to use as the name of
    // the identity alias?
    //
    Option(const Symbol*) label = nullptr;
    Init_Frame(out, exemplar, label, NONMETHOD);
    return zero;
}


//
//  Reframer_Dispatcher: C
//
// The REFRAMER native specializes out the FRAME! argument of the function
// being modified when it builds the interface.
//
// So the next thing to do is to fulfill the next function's frame without
// running it, in order to build a frame to put into that specialized slot.
// Then we run the reframer.
//
// !!! As a first cut we build on top of specialize, and look for the
// parameter by means of a particular labeled void.
//
Bounce Reframer_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_REFRAMER);

    Value* shim = Details_At(details, IDX_REFRAMER_SHIM);
    assert(Is_Frame(shim));

    Value* param_index = Details_At(details, IDX_REFRAMER_PARAM_INDEX);
    assert(Is_Integer(param_index));

    // First run ahead and make the frame we want from the feed.
    //
    // Note: We can't write the value directly into the arg (as this frame
    // may have been built by a higher level ADAPT or other function that
    // still holds references, and those references could be reachable by
    // code that runs to fulfill parameters...which could see partially
    // filled values).  And we don't want to overwrite L->out in case of
    // invisibility.  So the frame's spare cell is used.
    //
    bool error_on_deferred = true;
    Sink(Value) spare = SPARE;

    require (
      Init_Invokable_From_Feed(
        spare,
        nullptr,
        L->feed,
        error_on_deferred
    ));

    Atom* arg = Level_Arg(L, VAL_INT32(param_index));
    Move_Cell(arg, spare);

    Tweak_Level_Phase(L, Frame_Phase(shim));
    Tweak_Level_Coupling(L, Frame_Coupling(shim));

    return BOUNCE_REDO_CHECKED;  // the redo will use the updated phase & binding
}


//
//  Reframer_Details_Querier: C
//
bool Reframer_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Reframer_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_REFRAMER);

    switch (property) {
      case SYM_RETURN_OF: {
        Element* shim = cast(Element*, Details_At(details, IDX_REFRAMER_SHIM));
        assert(Is_Frame(shim));

        Details* shim_details = Phase_Details(Frame_Phase(shim));
        DetailsQuerier* querier = Details_Querier(shim_details);
        return (*querier)(out, shim_details, SYM_RETURN_OF); }

      default:
        break;
    }

    return false;
}


//
//  Alloc_Action_From_Exemplar: C
//
// Leaves details uninitialized, and lets you specify the dispatcher.
//
Details* Alloc_Action_From_Exemplar(
    ParamList* paramlist,
    Option(const Symbol*) label,
    Dispatcher* dispatcher,
    REBLEN details_capacity
){
    Phase* unspecialized = Frame_Phase(Phase_Archetype(paramlist));

    const Key* tail;
    const Key* key = Phase_Keys(&tail, unspecialized);
    const Param* param = Phase_Params_Head(unspecialized);
    Value* arg = Slot_Hack(Varlist_Slots_Head(paramlist));
    for (; key != tail; ++key, ++arg, ++param) {
        if (Is_Specialized(param))
            continue;

        // Leave non-hidden unspecialized args to be handled by the evaluator.
        //
        // https://forum.rebol.info/t/default-values-and-make-frame/1412
        // https://forum.rebol.info/t/1413
        //
        if (Is_Parameter(arg)) {
          #if DEBUG_POISON_UNINITIALIZED_CELLS
            Poison_Cell(arg);
          #endif
            Blit_Param_Unmarked(arg, param);
            continue;
        }

        heeded (Corrupt_Cell_If_Needful(Level_Spare(TOP_LEVEL)));
        heeded (Corrupt_Cell_If_Needful(Level_Scratch(TOP_LEVEL)));

        require (
          bool check = Typecheck_Coerce(TOP_LEVEL, param, arg, false)
        );
        if (not check)
            panic (Error_Arg_Type(label, key, param, arg));
    }

    DECLARE_ELEMENT (elem);
    Init_Frame(elem, paramlist, ANONYMOUS, NONMETHOD);

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED,
        elem,
        dispatcher,
        details_capacity
    );

    return details;
}


//
//  reframer: native [
//
//  "Make a function that manipulates an invocation at the callsite"
//
//      return: [action!]
//      shim "The action that has a FRAME! (or QUOTED?) argument to supply"
//          [<unrun> frame!]
//      :parameter "Shim parameter receiving the frame--defaults to last"
//          [word!]  ; parameter not checked for FRAME! type compatibility [1]
//  ]
//
DECLARE_NATIVE(REFRAMER)
//
// 1. At one time, the REFRAMER generator would typecheck a dummy FRAME! so
//    that at creation time you'd get an error if you specified a parameter
//    that wouldn't accept frames, vs. getting the error later.  This was
//    dodgy because there may be a more specific typecheck on the frame
//    than just "any frame".  There also aren't any obvious frames on hand
//    to use, so it used this invocation Level's frame...but that forced it
//    managed, which had cost.  The check was removed and so if you pick a
//    parameter that doesn't accept frames you'll just find out at call time.
//
// 2. We need the dispatcher to be willing to start the reframing step even
//    though the frame to be processed isn't ready yet.  So we have to
//    specialize the argument with something that type checks.  It wants a
//    FRAME!, so temporarily fill it with the exemplar frame itself.
//
//    !!! We could set CELL_FLAG_PARAM_NOTE_TYPECHECKED on the argument and
//    have it be some other placeholder.  See also SPECIALIZE:RELAX:
//
//      https://forum.rebol.info/t/generalized-argument-removal/2297
{
    INCLUDE_PARAMS_OF_REFRAMER;

    Phase* shim = Frame_Phase(ARG(SHIM));
    Option(const Symbol*) label = Frame_Label_Deep(ARG(SHIM));

    DECLARE_BINDER (binder);
    Construct_Binder(binder);
    ParamList* exemplar = Make_Varlist_For_Action_Push_Partials(
        ARG(SHIM),
        STACK_BASE,
        binder,
        nullptr  // no placeholder, leave parameter! antiforms
    );

    REBLEN param_index = 0;

    if (TOP_INDEX != STACK_BASE) {
        Destruct_Binder(binder);
        panic ("REFRAMER can't use partial specializions ATM");
    }

    const Key* key;
    const Param* param;

    if (Bool_ARG(PARAMETER)) {
        const Symbol* symbol = Word_Symbol(ARG(PARAMETER));
        param_index = opt Try_Get_Binder_Index(binder, symbol);
        if (param_index == 0) {
            Destruct_Binder(binder);
            panic (Error_No_Arg(label, symbol));
        }
        key = Varlist_Key(exemplar, param_index);
        param = cast(Param*, Varlist_Slot(exemplar, param_index));
    }
    else {
        param = Last_Unspecialized_Param(&key, shim);
        param_index = param - Phase_Params_Head(shim) + 1;
    }

    Destruct_Binder(binder);

    Value* var = Slot_Hack(
        Varlist_Slot(exemplar, param_index)  // "specialize" slot [2]
    );
    assert(Is_Parameter(var));
    Copy_Cell(var, Varlist_Archetype(exemplar));

    Manage_Stub(exemplar);

    Details* details = Alloc_Action_From_Exemplar(
        exemplar,  // shim minus the frame argument
        label,
        &Reframer_Dispatcher,
        MAX_IDX_REFRAMER  // details array capacity => [shim, param_index]
    );

    Copy_Cell(Details_At(details, IDX_REFRAMER_SHIM), Element_ARG(SHIM));
    Init_Integer(Details_At(details, IDX_REFRAMER_PARAM_INDEX), param_index);

    Init_Action(OUT, details, label, NONMETHOD);
    return UNSURPRISING(OUT);
}
