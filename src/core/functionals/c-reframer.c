//
//  File: %c-reframer.c
//  Summary: "Function that can transform arbitrary callsite functions"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
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
//         p: first parameters of f
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
    IDX_REFRAMER_MAX
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
//    but since it didn't get managed it drops the flag in Drop_Action().
//
//    !!! The flag is new, as a gambit to try and avoid copying frames for
//    DO-ing just in order to expire the old identity.  Under development.
//
// 3. The function did not actually execute, so L->varlist was never handed
//    out...the varlist should never have gotten managed.  So this context
//    can theoretically just be put back into the reuse list, or managed
//    and handed out for other purposes.  Caller's choice.
//
Level* Make_Pushed_Level_From_Action_Feed_May_Throw(
    Value* out,
    Value* action,
    Feed* feed,
    StackIndex base,
    bool error_on_deferred
){
    Level* L = Make_Level(
        &Action_Executor,
        feed,
        LEVEL_MASK_NONE  // FULFILL_ONLY added after Push_Action()
    );
    L->baseline.stack_base = base;  // incorporate refinements
    Freshen_Cell(out);
    Push_Level(out, L);

    if (error_on_deferred)  // can't deal with ELSE/THEN [1]
        L->flags.bits |= ACTION_EXECUTOR_FLAG_ERROR_ON_DEFERRED_INFIX;

    Push_Action(L, VAL_ACTION(action), Cell_Frame_Coupling(action));
    Begin_Action(L, VAL_FRAME_LABEL(action), PREFIX_0);

    Set_Executor_Flag(ACTION, L, FULFILL_ONLY);  // Push_Action() won't allow

    assert(Level_Coupling(L) == Cell_Frame_Coupling(action));  // no invocation

    if (Trampoline_With_Top_As_Root_Throws())
        return L;

    assert(Is_Nothing(L->out));  // should only have gathered arguments

    assert(  // !!! new flag [2]
        Not_Subclass_Flag(VARLIST, L->varlist, FRAME_HAS_BEEN_INVOKED)
    );

    assert(not (L->flags.bits & ACTION_EXECUTOR_FLAG_FULFILL_ONLY));

    L->u.action.original = VAL_ACTION(action);
    Tweak_Level_Phase(  // Drop_Action() cleared, restore
        L,
        ACT_IDENTITY(VAL_ACTION(action))
    );
    Tweak_Level_Coupling(L, Cell_Frame_Coupling(action));

    assert(Not_Node_Managed(L->varlist));  // shouldn't be [3]

    return L;  // may not be at end or thrown, e.g. (/x: does+ just y x = 'y)
}


//
//  Init_Invokable_From_Feed_Throws: C
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
bool Init_Invokable_From_Feed_Throws(
    Sink(Value) out,
    Option(const Element*) first,  // override first value, vs. At_Feed(feed)
    Feed* feed,
    bool error_on_deferred  // if not planning to keep running, can't ELSE/THEN
){
    const Element* v = first ? unwrap first : Try_At_Feed(feed);

    // !!! The case of `([x]: @)` wants to make something which when it
    // evaluates becomes invisible.  There's no QUOTED? value that can do
    // that, so if the feature is to be supported it needs to be VOID.
    //
    // Not all callers necessarily want to tolerate an end condition, so this
    // needs review.
    //
    if (v == nullptr) {  // no first, and feed was at end
        Freshen_Cell(out);
        return false;
    }

    // Unfortunately, it means that `[x y]: ^(eval f)` and `[x y]: ^ eval f`
    // can't work.  The problem is that you don't know how many expressions
    // will be involved in these cases, and the multi-return is a syntax trick
    // that can only work when interacting with one function, and even plain
    // groups break that guarantee.  Do meta values with e.g. `[^x y]: eval f`.
    //
    if (Any_Group(v))  // `requote (append [a b c] #d, <can't-work>)`
        fail ("Actions made with REFRAMER cannot work with GROUP!s");

    StackIndex base = TOP_INDEX;

    if (Is_Word(v) or Is_Tuple(v) or Is_Path(v) or Is_Chain(v))
        Get_Var_May_Fail(out, v, FEED_BINDING(feed));  // !!! throws?
    else
        Derelativize(out, v, FEED_BINDING(feed));

    if (not first)  // nothing passed in, so we used a feed value
        Fetch_Next_In_Feed(feed);  // we've seen it now

    if (not Is_Action(out)) {
        Quotify(out, 1);
        return false;
    }

    // !!! Process_Action_Throws() calls Drop_Action() and loses the phase.
    // It probably shouldn't, but since it does we need the action afterward
    // to put the phase back.
    //
    DECLARE_VALUE (action);
    Move_Cell(action, out);
    Push_GC_Guard(action);

    Option(const String*) label = VAL_FRAME_LABEL(action);

    Level* L = Make_Pushed_Level_From_Action_Feed_May_Throw(
        out,
        action,
        feed,
        base,
        error_on_deferred
    );

    if (Is_Throwing(L)) {  // signals threw
        Drop_Level(L);
        Drop_GC_Guard(action);
        return true;
    }

    // The exemplar may or may not be managed as of yet.  We want it
    // managed, but Push_Action() does not use ordinary series creation to
    // make its nodes, so manual ones don't wind up in the tracking list.
    //
    Action* act = VAL_ACTION(action);
    assert(Level_Coupling(L) == Cell_Frame_Coupling(action));

    assert(Not_Node_Managed(L->varlist));

    Array* varlist = L->varlist;  // !!! still is fulfilling?
    L->varlist = nullptr;  // don't let Drop_Level() free varlist (we want it)
    Tweak_Bonus_Keysource(varlist, ACT_KEYLIST(act));  // disconnect from f
    Drop_Level(L);
    Drop_GC_Guard(action);

    Set_Node_Managed_Bit(varlist);  // can't use Manage_Flex

    Init_Frame(out, cast(VarList*, varlist), label);
    return false;  // didn't throw
}


//
//  Init_Frame_From_Feed_Throws: C
//
// Making an invokable from a feed might return a QUOTED?, because that is
// more efficient (and truthful) than creating a FRAME! for the identity
// function.  However, MAKE FRAME! of a VARARGS! was an experimental feature
// that has to follow the rules of MAKE FRAME!...e.g. returning a frame.
// This converts QUOTED?s into frames for the identity function.
//
bool Init_Frame_From_Feed_Throws(
    Sink(Value) out,
    const Element* first,
    Feed* feed,
    bool error_on_deferred
){
    if (Init_Invokable_From_Feed_Throws(out, first, feed, error_on_deferred))
        return true;

    if (Is_Frame(out))
        return false;

    assert(Is_Quoted(out));
    VarList* exemplar = Make_Varlist_For_Action(
        Lib(IDENTITY),
        TOP_INDEX,
        nullptr
    );

    Unquotify(Copy_Cell(Varlist_Slot(exemplar, 2), out), 1);

    // Should we save the WORD! from a variable access to use as the name of
    // the identity alias?
    //
    Option(const Symbol*) label = nullptr;
    Init_Frame(out, exemplar, label);
    return false;
}


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

    Details* details = Phase_Details(PHASE);
    assert(Array_Len(details) == IDX_REFRAMER_MAX);

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
    if (Init_Invokable_From_Feed_Throws(
        SPARE,
        nullptr,
        L->feed,
        error_on_deferred
    )){
        return THROWN;
    }

    Value* arg = Level_Arg(L, VAL_INT32(param_index));
    Move_Cell(arg, stable_SPARE);

    Tweak_Level_Phase(L, ACT_IDENTITY(VAL_ACTION(shim)));
    Tweak_Level_Coupling(L, Cell_Frame_Coupling(shim));

    return BOUNCE_REDO_CHECKED;  // the redo will use the updated phase & binding
}


//
//  /reframer: native [
//
//  "Make a function that manipulates an invocation at the callsite"
//
//      return: [action?]
//      shim "The action that has a FRAME! (or QUOTED?) argument to supply"
//          [<unrun> frame!]
//      :parameter "Shim parameter receiving the frame--defaults to last"
//          [word!]
//  ]
//
DECLARE_NATIVE(reframer)
{
    INCLUDE_PARAMS_OF_REFRAMER;

    Action* shim = VAL_ACTION(ARG(shim));
    Option(const Symbol*) label = VAL_FRAME_LABEL(ARG(shim));

    StackIndex base = TOP_INDEX;

    struct Reb_Binder binder;
    INIT_BINDER(&binder);
    VarList* exemplar = Make_Varlist_For_Action_Push_Partials(
        ARG(shim),
        base,
        &binder
    );

    Option(Error*) error = nullptr;  // can't fail() with binder in effect

    REBLEN param_index = 0;

    if (TOP_INDEX != base) {
        error = Error_User("REFRAMER can't use partial specializions ATM");
        goto cleanup_binder;
    }

  blockscope {
    const Key* key;
    const Param* param;

    if (REF(parameter)) {
        const Symbol* symbol = Cell_Word_Symbol(ARG(parameter));
        param_index = Get_Binder_Index_Else_0(&binder, symbol);
        if (param_index == 0) {
            error = Error_No_Arg(label, symbol);
            goto cleanup_binder;
        }
        key = Varlist_Key(exemplar, param_index);
        param = cast_PAR(Varlist_Slot(exemplar, param_index));
    }
    else {
        param = Last_Unspecialized_Param(&key, shim);
        param_index = param - ACT_PARAMS_HEAD(shim) + 1;
    }

    // Make sure the parameter is able to accept FRAME! arguments (the type
    // checking will ultimately use the same slot we overwrite here!)
    //
    // !!! This checks to see if it accepts *an* instance of a frame, but it's
    // not narrow enough because there might be some additional check on the
    // properties of the frame.  This is a limit of the type constraints
    // needing an instance of the type to check.  It may suggest that we
    // shouldn't do this at all, and just let it fail when called.  :-/
    //
    Copy_Cell(SPARE, LEVEL->rootvar);
    if (not Typecheck_Coerce_Argument(param, SPARE)) {
        DECLARE_ATOM (label_word);
        if (label)
            Init_Word(label_word, unwrap label);
        else
            Init_Blank(label_word);

        DECLARE_ATOM (param_word);
        Init_Word(param_word, Key_Symbol(key));

        error = Error_Expect_Arg_Raw(
            label_word,
            Datatype_From_Kind(REB_FRAME),
            param_word
        );
        goto cleanup_binder;
    }
  }

  cleanup_binder: {
    const Key* tail;
    const Key* key = ACT_KEYS(&tail, shim);
    const Param* param = ACT_PARAMS_HEAD(shim);
    for (; key != tail; ++key, ++param) {
        if (Is_Specialized(param))
            continue;

        const Symbol* symbol = Key_Symbol(key);
        REBLEN index = Remove_Binder_Index_Else_0(&binder, symbol);
        assert(index != 0);
        UNUSED(index);
    }

    SHUTDOWN_BINDER(&binder);

    if (error)  // once binder is cleaned up, safe to raise errors
        fail (unwrap error);
  }

    // We need the dispatcher to be willing to start the reframing step even
    // though the frame to be processed isn't ready yet.  So we have to
    // specialize the argument with something that type checks.  It wants a
    // FRAME!, so temporarily fill it with the exemplar frame itself.
    //
    // !!! An expired frame would be better, or tweaking the argument so it
    // takes a void and giving it ~pending~; would make bugs more obvious.
    //
    Value* var = Varlist_Slot(exemplar, param_index);
    assert(Not_Specialized(var));
    Copy_Cell(var, Varlist_Archetype(exemplar));

    // Make action with enough space to store the implementation phase and
    // which parameter to fill with the *real* frame instance.
    //
    Manage_Flex(exemplar);
    Phase* reframer = Alloc_Action_From_Exemplar(
        exemplar,  // shim minus the frame argument
        label,
        &Reframer_Dispatcher,
        IDX_REFRAMER_MAX  // details array capacity => [shim, param_index]
    );

    Details* details = Phase_Details(reframer);
    Copy_Cell(Details_At(details, IDX_REFRAMER_SHIM), ARG(shim));
    Init_Integer(Details_At(details, IDX_REFRAMER_PARAM_INDEX), param_index);

    return Init_Action(OUT, reframer, label, UNBOUND);
}
