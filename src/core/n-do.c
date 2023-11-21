//
//  File: %n-do.c
//  Summary: "native functions for DO, EVAL, APPLY"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Ren-C's philosophy of DO is that the argument to it represents a place to
// find source code.  Hence `DO 3` does not evaluate to the number 3, any
// more than `DO "print hello"` would evaluate to `"print hello"`.  If a
// generalized evaluator is needed, use the special-purpose REEVAL(UATE).
//
// Note that although the code for running blocks and frames is implemented
// here as C, the handler for processing STRING!, FILE!, TAG!, URL!, etc. is
// dispatched out to some Rebol code.  See `system/intrinsic/do*`.
//

#include "sys-core.h"


//
//  reeval: native [
//
//  {Process an evaluated argument *inline* as the evaluator loop would}
//
//      return: [<opt> <void> nihil? any-value!]
//      value [element?]
//          {BLOCK! passes-thru, ACTION! runs, SET-WORD! assigns...}
//      expressions [<opt> element? <variadic>]
//          {Depending on VALUE, more expressions may be consumed}
//  ]
//
DECLARE_NATIVE(reeval)
{
    INCLUDE_PARAMS_OF_REEVAL;

    // REEVAL only *acts* variadic, but uses ST_EVALUATOR_REEVALUATING
    //
    UNUSED(ARG(expressions));

    REBVAL *v = ARG(value);

    bool enfix =
        Is_Quasi(v)
        and HEART_BYTE(v) == REB_FRAME
        and Is_Enfixed(v);

    Flags flags = LEVEL_MASK_NONE;

    if (Reevaluate_In_Sublevel_Throws(
        OUT,  // reeval :comment "this should leave old input"
        level_,
        ARG(value),
        flags,
        enfix
    )){
        return THROWN;
    }

    return OUT;
}


//
//  shove: native [
//
//  {Shove a parameter into an ACTION! as its first argument}
//
//      return: [<opt> any-value!]
//          "REVIEW: How might this handle shoving enfix invisibles?"
//      :left [<end> <opt> any-value!]
//          "Requests parameter convention based on enfixee's first argument"
//      'right [<variadic> <end> any-value!]
//          "(uses magic -- SHOVE can't be written easily in usermode yet)"
//      /prefix "Force either prefix or enfix behavior (vs. acting as is)"
//          [logic!]
//  ]
//
DECLARE_NATIVE(shove)
//
// PATH!s do not do infix lookup in Rebol, and there are good reasons for this
// in terms of both performance and semantics.  However, it is sometimes
// needed to dispatch via a path--for instance to call an enfix function that
// lives in a context, or even to call one that has refinements.
//
// The SHOVE operation is used to push values from the left to act as the
// first argument of an operation, e.g.:
//
//      >> 10 >- lib.(print "Hi!" first [multiply]) 20
//      Hi!
//      200
//
// It's becoming more possible to write something like this in usermode, but
// it would be inefficient, and there are binding problems to worry about
// in macro-like code.
{
    INCLUDE_PARAMS_OF_SHOVE;

    Level* L;
    if (not Is_Level_Style_Varargs_May_Fail(&L, ARG(right)))
        fail ("SHOVE (>-) not implemented for MAKE VARARGS! [...] yet");

    REBVAL *left = ARG(left);

    if (Is_Level_At_End(L))  // shouldn't be for WORD!/PATH! unless APPLY
        return COPY(ARG(left));  // ...because evaluator wants `help <-` to work

    // It's best for SHOVE to do type checking here, as opposed to setting
    // some kind of LEVEL_FLAG_SHOVING and passing that into the evaluator, then
    // expecting it to notice if you shoved into an INTEGER! or something.
    //
    // !!! To get the feature working as a first cut, this doesn't try get too
    // fancy with apply-like mechanics and slipstream refinements on the
    // stack to enfix functions with refinements.  It specializes the ACTION!.
    // We can do better, but seeing as how you couldn't call enfix actions
    // with refinements *at all* before, this is a step up.

    REBVAL *shovee = ARG(right); // reuse arg cell for the shoved-into
    Option(const Symbol*) label = nullptr;

    if (IS_WORD(At_Level(L)) or IS_PATH(At_Level(L)) or IS_TUPLE(At_Level(L))) {
        //
        // !!! should get label from word
        //
        Get_Var_May_Fail(
            OUT, // can't eval directly into arg slot
            At_Level(L),
            Level_Specifier(L),
            false
        );
        Move_Cell(shovee, OUT);
    }
    else if (IS_GROUP(At_Level(L))) {
        if (Do_Any_Array_At_Throws(OUT, At_Level(L), Level_Specifier(L)))
            return THROWN;

        Move_Cell(shovee, OUT);  // can't eval directly into arg slot
    }
    else
        Copy_Cell(shovee, SPECIFIC(At_Level(L)));

    Deactivate_If_Activation(shovee);  // allow ACTION! to be run

    if (not IS_FRAME(shovee))
        fail ("SHOVE's immediate right must be ACTION! or SET-XXX! type");

    if (not label)
        label = VAL_FRAME_LABEL(shovee);

    // Basic operator `>-` will use the enfix status of the shovee.
    // `->-` will force enfix evaluator behavior even if shovee is prefix.
    // `>--` will force prefix evaluator behavior even if shovee is enfix.
    //
    bool enfix;
    if (REF(prefix))
        enfix = not VAL_LOGIC(ARG(prefix));
    else if (IS_FRAME(shovee))
        enfix = Is_Enfixed(shovee);
    else
        enfix = false;

    Fetch_Next_Forget_Lookback(L);

    // Since we're simulating enfix dispatch, we need to move the first arg
    // where enfix gets it from...the frame output slot.
    //
    // We quoted the argument on the left, but the ACTION! we are feeding
    // into may want it evaluative.  (Enfix handling itself does soft quoting)
    //
    if (
        Get_Cell_Flag(left, UNEVALUATED)
        and Not_Subclass_Flag(
            VARLIST,
            ACT_PARAMLIST(VAL_ACTION(shovee)),
            PARAMLIST_QUOTES_FIRST
        )
    ){
        if (Eval_Value_Throws(OUT, left, SPECIFIED))
            return THROWN;
    }
    else {
        Copy_Cell(OUT, left);
        if (Get_Cell_Flag(left, UNEVALUATED))
            Set_Cell_Flag(OUT, UNEVALUATED);
    }

    Flags flags = FLAG_STATE_BYTE(ST_ACTION_FULFILLING_ENFIX_FROM_OUT);

    Level* sub = Make_Level(level_->feed, flags);
    Push_Action(sub, VAL_ACTION(shovee), VAL_FRAME_BINDING(shovee));
    Begin_Action_Core(sub, label, enfix);

    Push_Level(OUT, sub);
    return DELEGATE_SUBLEVEL(sub);
}


//
//  do: native [
//
//  {Evaluates source code (see also EVAL for stepwise or invisible evaluation)}
//
//      return: [<opt> <void> any-value!]
//      source "Block of code, or indirect specification to find/make it" [
//          <maybe>  ; opts out of the DO, returns null
//          block!  ; source code in block form (see EVALUATE for other kinds)
//          text!  ; source code in text form
//          binary!  ; treated as UTF-8
//          url!  ; load code from URL via protocol
//          file!  ; load code from file on local disk
//          tag!  ; load relative to system.script.name
//          the-word!  ; module name (URL! looked up from table)
//          error!  ; should use FAIL instead
//          frame!  ; acts like APPLY (voids are optionals, not unspecialized)
//          activation?  ; will only run arity 0 actions (avoids DO variadic)
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//      /args "Sets system.script.args if doing a script (usually a TEXT!)"
//          [any-value!]
//      /only "Don't catch QUIT (default behavior for BLOCK!)"
//  ]
//
DECLARE_NATIVE(do)
// 2. FAIL is the preferred operation for triggering errors, as it has a
//    natural behavior for blocks passed to construct readable messages and
//    "FAIL X" more clearly communicates a failure than "DO X".  But DO of an
//    ERROR! would have to raise an error anyway, so it might as well raise the
//    one it is given.
//
// 3. There's an error given if you try to run a continuation of an ACTION!
//    and it takes a parameter, but you specify END as the WITH.  But giving
//    a special error here--that can point people to "did you mean REEVALUATE"
//    is something that is probably helpful enough to add.
{
    INCLUDE_PARAMS_OF_DO;

    REBVAL *source = ARG(source);

    Tweak_Non_Const_To_Explicitly_Mutable(source);

  #if !defined(NDEBUG)
    Set_Cell_Flag(source, PROTECTED);  // maybe only GC reference, keep!
  #endif

    Deactivate_If_Activation(source);

    switch (VAL_TYPE(source)) {
      case REB_BLOCK :  // no REB_GROUP, etc...EVAL does that.  [1]
        return DELEGATE(OUT, source);

      case REB_VARARGS : {
        REBVAL *position;
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            if (Do_Any_Array_At_Throws(OUT, position, SPECIFIED)) {
                //
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Trash(position);
                return THROWN;
            }

            Erase_Cell(position); // convention for shared data at endpoint

            if (Is_Void(OUT))
                return VOID;
            return OUT;
        }

        Level* L;
        if (not Is_Level_Style_Varargs_May_Fail(&L, source))
            panic (source); // Frame is the only other type

        // By definition, we are in the middle of a function call in the level
        // the varargs came from.  It's still on the stack, and we don't want
        // to disrupt its state.  Use a sublevel.

        if (Is_Level_At_End(L))
            return VOID;

        Level* sub = Make_Level(
            L->feed,
            LEVEL_MASK_NONE
        );
        sub->executor = &Array_Executor;
        Push_Level(OUT, sub);
        return DELEGATE_SUBLEVEL(sub); }

      case REB_THE_WORD : goto do_helper;
      case REB_BINARY : goto do_helper;
      case REB_TEXT : goto do_helper;
      case REB_URL : goto do_helper;
      case REB_FILE : goto do_helper;
      case REB_TAG : goto do_helper;

      do_helper : {
        UNUSED(REF(args)); // detected via `value? :arg`

        rebPushContinuation(
            cast(REBVAL*, OUT),  // <-- output cell
            LEVEL_MASK_NONE,
            rebRUN(SysUtil(DO_P)),
                source,
                rebQ(ARG(args)),
                REF(only) ? rebQ(Lib(TRUE)) : rebQ(Lib(FALSE))
        );
        return BOUNCE_DELEGATE; }

      case REB_ERROR :
        fail (VAL_CONTEXT(source));  // would fail anyway [2]

      case REB_FRAME : {
        if (Is_Frame_Details(source))
            if (First_Unspecialized_Param(nullptr, VAL_ACTION(source)))
                fail (Error_Do_Arity_Non_Zero_Raw());  // specific error?  [3]

        return DELEGATE(OUT, source); }

      default :
        break;
    }

    fail (Error_Do_Arity_Non_Zero_Raw());  // https://trello.com/c/YMAb89dv
}


//
//  evaluate: native [
//
//  {Perform a single evaluator step, returning the next source position}
//
//      return: "Value from the step"
//          [<opt> <void> nihil? any-value!]
//      source [
//          <maybe>  ; useful for `evaluate try ...` scenarios when no match
//          any-array!  ; source code in block form
//          activation?
//          frame!
//          varargs!  ; simulates as if frame! or block! is being executed
//      ]
//      /next "Do one step of evaluation"
//          [word! tuple!]  ; !!! does not use multi-return, see 1
//  ]
//
DECLARE_NATIVE(evaluate)
//
// 1. Having a function like EVALUATE itself be multi-return is a pain, as
//    it is trying to return a result that can itself be a multi-return.
//    This is the nature of anything that does proxying.  It's *technically*
//    possible for a caller to pick parameter packs out of parameter packs,
//    but inconvenient.  Especially considering that stepwise evaluation is
//    going to be done on some kind of "evaluator state"--not just a block,
//    that state should be updated.
//
// 2. We want EVALUATE to treat all ANY-ARRAY! the same.  (e.g. a ^[1 + 2] just
//    does the same thing as [1 + 2] and gives 3, not '3)  Rather than mutate
//    the cell to plain BLOCK! and pass it to CONTINUE_CORE(), we initialize
//    a feed from the array directly.
//
// 6. There may have been a LET statement in the code.  If there was, we have
//    to incorporate the binding it added into the reported state *somehow*.
//    Right now we add it to the block we give back...this gives rise to
//    questionable properties, such as if the user goes backward in the block
//    and were to evaluate it again:
//
//      https://forum.rebol.info/t/1496
//
//    Right now we can politely ask "don't do that", but better would probably
//    be to make EVALUATE return something with more limited privileges... more
//    like a FRAME!/VARARGS!.
{
    INCLUDE_PARAMS_OF_EVALUATE;

    REBVAL *rest_var = ARG(next);
    REBVAL *source = ARG(source);  // may be only GC reference, don't lose it!

    enum {
        ST_EVALUATE_INITIAL_ENTRY = STATE_0,
        ST_EVALUATE_SINGLE_STEPPING
    };

    switch (STATE) {
      case ST_EVALUATE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_EVALUATE_SINGLE_STEPPING :
        goto single_step_result_in_out;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Deactivate_If_Activation(source);
    Tweak_Non_Const_To_Explicitly_Mutable(source);

  #if !defined(NDEBUG)
    Set_Cell_Flag(ARG(source), PROTECTED);
  #endif

    if (ANY_ARRAY(source)) {
        if (VAL_LEN_AT(source) == 0) {  // `evaluate []` is invisible intent
            if (REF(next))
                rebElide(Canon(SET), rebQ(rest_var), nullptr);

            Init_Nihil(OUT);  // !!! Callers not prepared for more ornery result
            return Proxy_Multi_Returns(level_);
        }

        Feed* feed = Make_At_Feed_Core(  // use feed [2]
            source,
            SPECIFIED
        );
        assert(Not_Feed_At_End(feed));

        Flags flags = LEVEL_FLAG_ALLOCATED_FEED;

        if (not REF(next)) {
            flags |= FLAG_STATE_BYTE(ST_ARRAY_PRELOADED_ENTRY);
            Init_Nihil(OUT);  // heeded by array executor
        }

        Level* sub = Make_Level(feed, flags);
        Push_Level(OUT, sub);

        if (not REF(next)) {  // plain evaluation to end, maybe invisible
            sub->executor = &Array_Executor;
            return DELEGATE_SUBLEVEL(sub);
        }

        Set_Level_Flag(sub, TRAMPOLINE_KEEPALIVE);  // to ask how far it got

        STATE = ST_EVALUATE_SINGLE_STEPPING;
        return CONTINUE_SUBLEVEL(sub);
    }
    else switch (VAL_TYPE(source)) {

      case REB_FRAME : {
        //
        // !!! It is likely that the return result for the NEXT: will actually
        // be a FRAME! when the input to EVALUATE is a BLOCK!, so that the
        // LET bindings can be preserved.  Binding is still a mess when it
        // comes to questions like backtracking in blocks, so review.
        //
        if (REF(next))
            fail ("/NEXT Behavior not implemented for FRAME! in EVALUATE");

        if (Is_Frame_Details(source))
            if (First_Unspecialized_Param(nullptr, VAL_ACTION(source)))
                fail (Error_Do_Arity_Non_Zero_Raw());  // see notes in DO on error

        return DELEGATE(OUT, source); }

      case REB_VARARGS : {
        assert(IS_VARARGS(source));

        REBVAL *position;
        if (Is_Block_Style_Varargs(&position, source)) {
            //
            // We can execute the array, but we must "consume" elements out
            // of it (e.g. advance the index shared across all instances)
            //
            // !!! If any VARARGS! op does not honor the "locked" flag on the
            // array during execution, there will be problems if it is TAKE'n
            // or DO'd while this operation is in progress.
            //
            REBLEN index;
            if (Eval_Step_In_Any_Array_At_Throws(
                SPARE,
                &index,
                position,
                SPECIFIED,
                LEVEL_MASK_NONE
            )){
                // !!! A BLOCK! varargs doesn't technically need to "go bad"
                // on a throw, since the block is still around.  But a FRAME!
                // varargs does.  This will cause an assert if reused, and
                // having BLANK! mean "thrown" may evolve into a convention.
                //
                Init_Trash(position);
                return THROWN;
            }

            VAL_INDEX_UNBOUNDED(position) = index;
        }
        else {
            Level* L;
            if (not Is_Level_Style_Varargs_May_Fail(&L, source))
                panic (source); // Frame is the only other type

            // By definition, we're in the middle of a function call in level
            // the varargs came from.  It's still on the stack--we don't want
            // to disrupt its state (beyond feed advancing).  Use a sublevle.

            if (Is_Level_At_End(L))
                return nullptr;

            Flags flags = LEVEL_MASK_NONE;
            if (Eval_Step_In_Sublevel_Throws(SPARE, L, flags))
                return THROWN;
        }
        break; }

      default:
        fail (PARAM(source));
    }

    if (REF(next))
        rebElide(Canon(SET), rebQ(rest_var), source);

    return COPY(SPARE);

} single_step_result_in_out: {  //////////////////////////////////////////////

    Specifier* specifier = Level_Specifier(SUBLEVEL);
    VAL_INDEX_UNBOUNDED(source) = Level_Array_Index(SUBLEVEL);  // new index
    Drop_Level(SUBLEVEL);

    INIT_BINDING_MAY_MANAGE(source, specifier);  // integrate LETs [6]

    if (REF(next))
        rebElide(Canon(SET), rebQ(rest_var), source);

    return OUT;
}}


//
//  redo: native [
//
//  {Restart a frame's action from the top with its current state}
//
//      return: []  ; !!! notation for divergent function?
//      restartee "Frame to restart, or bound word (e.g. REDO 'RETURN)"
//          [frame! any-word!]
//      /other "Restart in a frame-compatible function (sibling tail-call)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(redo)
//
// This can be used to implement tail-call recursion:
//
// https://en.wikipedia.org/wiki/Tail_call
//
{
    INCLUDE_PARAMS_OF_REDO;

    REBVAL *restartee = ARG(restartee);
    if (not IS_FRAME(restartee)) {
        if (not Did_Get_Binding_Of(OUT, restartee))
            fail ("No context found from restartee in REDO");

        if (not IS_FRAME(OUT))
            fail ("Context of restartee in REDO is not a FRAME!");

        Move_Cell(restartee, OUT);
    }

    Context* c = VAL_CONTEXT(restartee);

    Level* L = CTX_LEVEL_IF_ON_STACK(c);
    if (L == NULL)
        fail ("Use DO to start a not-currently running FRAME! (not REDO)");

    // If we were given a sibling to restart, make sure it is frame compatible
    // (e.g. the product of ADAPT-ing, CHAIN-ing, ENCLOSE-ing, HIJACK-ing a
    // common underlying function).
    //
    // !!! It is possible for functions to be frame-compatible even if they
    // don't come from the same heritage (e.g. two functions that take an
    // INTEGER! and have 2 locals).  Such compatibility may seem random to
    // users--e.g. not understanding why a function with 3 locals is not
    // compatible with one that has 2, and the test would be more expensive
    // than the established check for a common "ancestor".
    //
    if (REF(other)) {
        REBVAL *sibling = ARG(other);
        if (
            ACT_KEYLIST(L->u.action.original)
            != ACT_KEYLIST(VAL_ACTION(sibling))
        ){
            fail ("/OTHER function passed to REDO has incompatible FRAME!");
        }

        INIT_VAL_FRAME_PHASE(restartee, ACT_IDENTITY(VAL_ACTION(sibling)));
        INIT_VAL_FRAME_BINDING(restartee, VAL_FRAME_BINDING(sibling));
    }

    // We need to cooperatively throw a restart instruction up to the level
    // of the frame.  Use REDO as the throw label that Eval_Core() will
    // identify for that behavior.
    //
    Copy_Cell(SPARE, Lib(REDO));
    INIT_VAL_FRAME_BINDING(SPARE, c);

    // The FRAME! contains its ->phase and ->binding, which should be enough
    // to restart the phase at the point of parameter checking.  Make that
    // the actual value that Eval_Core() catches.
    //
    return Init_Thrown_With_Label(LEVEL, restartee, stable_SPARE);
}


//
//  applique: native [
//
//  {Invoke an ACTION! with all required arguments specified}
//
//      return: [<opt> <void> any-value!]
//      operation [<unrun> frame!]
//      def "Frame definition block (will be bound and evaluated)"
//          [block!]
//  ]
//
DECLARE_NATIVE(applique)
//
// 1. Make a FRAME! for the ACTION!, weaving in the ordered refinements
//    collected on the stack (if any).  Any refinements that are used in any
//    specialization level will be pushed as well, which makes them out
//    prioritize (e.g. higher-ordered) than any used in a PATH! that were
//    pushed during the Get of the ACTION!.
{
    INCLUDE_PARAMS_OF_APPLIQUE;

    Value(*) op = ARG(operation);
    Value(*) def = ARG(def);

    Value(*) frame = ARG(return);  // reuse as GC-safe cell for FRAME!

    enum {
        ST_APPLIQUE_INITIAL_ENTRY = STATE_0,
        ST_APPLIQUE_RUNNING_DEF_BLOCK
    };

    switch (STATE) {
      case ST_APPLIQUE_INITIAL_ENTRY :
        goto initial_entry;

      case ST_APPLIQUE_RUNNING_DEF_BLOCK :
        goto definition_result_in_spare;

      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Context* exemplar = Make_Context_For_Action_Push_Partials(  // [1]
        op,
        STACK_BASE,  // lowest_ordered_dsp of refinements to weave in
        nullptr,  // no binder needed
        NONE_CELL  // seen as unspecialized by ST_ACTION_TYPECHECKING
    );
    Manage_Series(CTX_VARLIST(exemplar));
    Init_Frame(frame, exemplar, VAL_FRAME_LABEL(op));

    Drop_Data_Stack_To(STACK_BASE);  // refinement order unimportant

    Virtual_Bind_Deep_To_Existing_Context(
        def,
        exemplar,
        nullptr,  // !!! Unused binder parameter
        REB_SET_WORD
    );

    STATE = ST_APPLIQUE_RUNNING_DEF_BLOCK;
    return CONTINUE(SPARE, def);  // first run block bound to frame

} definition_result_in_spare: {  /////////////////////////////////////////////

    return DELEGATE(OUT, frame);  // now run the frame
}}


//
//  apply: native [
//
//  {Invoke an action with all required arguments specified}
//
//      return: [<opt> <void> any-value!]
//      operation [<unrun> frame!]
//      args "Arguments and Refinements, e.g. [arg1 arg2 /ref refine1]"
//          [block!]
//      /relax "Don't worry about too many arguments to the APPLY"
//      <local> frame index
//  ]
//
DECLARE_NATIVE(apply)
//
// 1. Binders cannot be held across evaluations at this time.  Do slow
//    lookups for refinements, but this is something that needs rethinking.
//
// 2. Make a FRAME! for the ACTION!, weaving in the ordered refinements
//    collected on the stack (if any).  Any refinements that are used in any
//    specialization level will be pushed as well, which makes them
//    out-prioritize (e.g. higher-ordered) than any used in a PATH! that were
//    pushed during the Get of the ACTION!.
//
// 3. Two argument-name labels in a row is not legal...treat it like the next
//    refinement is reaching a comma or end of block.  (Though this could be
//    treated as an <end> case?)
//
// 4. We treat <skip> parameters as if they can only be requested by name,
//    like a refinement.  This is because the evaluative nature of APPLY is
//    not compatible with the quoting requirement of skippability.
//
// 5. Low-level frame mechanics require that no-argument refinements be either
//    # or null.  As a higher-level utility, APPLY can throw in some assistance
//    so it converts (true => #) and (false => null)
//
// 6. We need to remove the binder indices, whether we are raising an error
//    or not.  But we also want any fields not assigned to be set to `~`.
//    (We wanted to avoid the situation where someone purposefully set a
//    meta-parameter to `~` being interpreted as never setting a field).
{
    INCLUDE_PARAMS_OF_APPLY;

    Value(*) op = ARG(operation);
    Value(*) args = ARG(args);

    Value(*) frame = ARG(frame);  // local variable for holding GC-safe frame
    Value(*) iterator = ARG(return);  // reuse to hold Evars iterator

    Value(*) var;  // may come from evars iterator or found by index
    Param* param;  // (same)

    enum {
        ST_APPLY_INITIAL_ENTRY = STATE_0,
        ST_APPLY_LABELED_EVAL_STEP,
        ST_APPLY_UNLABELED_EVAL_STEP
    };

    if (Get_Level_Flag(level_, ABRUPT_FAILURE))  // a fail() in this dispatcher
        goto finalize_apply;

    switch (STATE) {
      case ST_APPLY_INITIAL_ENTRY :
        goto initial_entry;

      case ST_APPLY_LABELED_EVAL_STEP :
        if (THROWING)
            goto finalize_apply;
        goto labeled_step_result_in_spare;

      case ST_APPLY_UNLABELED_EVAL_STEP :
        if (THROWING)
            goto finalize_apply;
        if (Is_None(iterator)) {
            assert(REF(relax));
            goto handle_next_item;
        }
        goto unlabeled_step_result_in_spare;

      default : assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    /*struct Reb_Binder binder;  // [1]
    INIT_BINDER(&binder);*/
    Context* exemplar = Make_Context_For_Action_Push_Partials(  // [2]
        op,
        STACK_BASE, // lowest_ordered_dsp of refinements to weave in
        nullptr /* &binder */,
        NONE_CELL
    );
    Manage_Series(CTX_VARLIST(exemplar)); // Putting into a frame
    Init_Frame(frame, exemplar, VAL_FRAME_LABEL(op));  // GC guarded

    Drop_Data_Stack_To(STACK_BASE);  // partials ordering unimportant

    Level* L = Make_Level_At(
        args,
        LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
    );
    Push_Level(SPARE, L);

    EVARS *e = Try_Alloc(EVARS);
    Init_Evars(e, frame);  // CTX_ARCHETYPE(exemplar) is phased, sees locals
    Init_Handle_Cdata(iterator, e, sizeof(EVARS));

    Set_Level_Flag(level_, NOTIFY_ON_ABRUPT_FAILURE);  // to clean up iterator
    goto handle_next_item;

} handle_next_item: {  ///////////////////////////////////////////////////////

    Level* L = SUBLEVEL;

    if (Is_Level_At_End(L))
        goto finalize_apply;

    const Cell* at = At_Level(L);

    if (IS_COMMA(at)) {
        Fetch_Next_Forget_Lookback(L);
        goto handle_next_item;
    }

    // We do special handling if we see a /REFINEMENT ... that is taken
    // to mean we are naming the next argument.

  #if !defined(NDEBUG)
    Trash_Pointer_If_Debug(param);
  #endif

    if (IS_PATH(at) and IS_REFINEMENT(at)) {
        STATE = ST_APPLY_LABELED_EVAL_STEP;

        const Symbol* symbol = VAL_REFINEMENT_SYMBOL(At_Level(L));

        REBLEN index = Find_Symbol_In_Context(frame, symbol, false);
        if (index == 0)
            fail (Error_Bad_Parameter_Raw(rebUnrelativize(at)));

        var = CTX_VAR(VAL_CONTEXT(frame), index);
        param = ACT_PARAM(VAL_ACTION(op), index);

        if (not Is_None(var))
            fail (Error_Bad_Parameter_Raw(rebUnrelativize(at)));

        const Cell* lookback = Lookback_While_Fetching_Next(L);  // for error
        at = Try_At_Level(L);

        if (at == nullptr or IS_COMMA(at))
            fail (Error_Need_Non_End_Raw(rebUnrelativize(lookback)));

        if (IS_PATH(at) and IS_REFINEMENT(at))  // [3]
            fail (Error_Need_Non_End_Raw(rebUnrelativize(lookback)));

        Init_Integer(ARG(index), index);
    }
    else if (Is_None(iterator)) {
        STATE = ST_APPLY_UNLABELED_EVAL_STEP;
        param = nullptr;  // throw away result
    }
    else {
        STATE = ST_APPLY_UNLABELED_EVAL_STEP;

        EVARS *e = VAL_HANDLE_POINTER(EVARS, iterator);

        while (true) {
            if (not Did_Advance_Evars(e)) {
                if (not REF(relax))
                    fail (Error_Apply_Too_Many_Raw());

                Free(EVARS, e);
                Init_None(iterator);
                param = nullptr;  // we're throwing away the evaluated product
                break;
            }

            if (
                VAL_PARAM_CLASS(e->param) == PARAM_CLASS_RETURN
                or VAL_PARAM_CLASS(e->param) == PARAM_CLASS_OUTPUT
                or GET_PARAM_FLAG(e->param, REFINEMENT)
                or GET_PARAM_FLAG(e->param, SKIPPABLE)
            ){
                Init_None(e->var);  // TBD: RETURN will be a pure local
                continue;  // skippable only requested by name [4]
            }
            if (Is_None(e->var)) {
                param = e->param;
                break;
            }
        }
    }

    assert(not Is_Pointer_Trash_Debug(param));  // nullptr means toss result

    if (param and VAL_PARAM_CLASS(param) == PARAM_CLASS_META)
        Set_Level_Flag(SUBLEVEL, META_RESULT);  // get decayed result otherwise
    else
        Clear_Level_Flag(SUBLEVEL, META_RESULT);

    Restart_Evaluator_Level(SUBLEVEL);
    return CATCH_CONTINUE_SUBLEVEL(SUBLEVEL);

} labeled_step_result_in_spare: {  ///////////////////////////////////////////

    REBLEN index = VAL_UINT32(ARG(index));

    var = CTX_VAR(VAL_CONTEXT(frame), index);
    param = ACT_PARAM(VAL_ACTION(op), index);

    goto copy_spare_to_var_in_frame;

} unlabeled_step_result_in_spare: {  /////////////////////////////////////////

    EVARS *e = VAL_HANDLE_POINTER(EVARS, iterator);

    var = e->var;
    param = e->param;

    goto copy_spare_to_var_in_frame;

} copy_spare_to_var_in_frame: {  /////////////////////////////////////////////

    if (  // help convert logic for no-arg refinement [5]
        IS_LOGIC(SPARE)
        and GET_PARAM_FLAG(param, REFINEMENT)
        and Is_Parameter_Unconstrained(param)
    ){
        if (VAL_LOGIC(SPARE))
            Init_Blackhole(var);
        else
            Init_Nulled(var);

        FRESHEN(SPARE);
    }
    else {
        Move_Cell(var, SPARE);
    }

    goto handle_next_item;

} finalize_apply: {  /////////////////////////////////////////////////////////

    if (Is_None(iterator))
        assert(REF(relax));
    else {
        EVARS *e = VAL_HANDLE_POINTER(EVARS, iterator);
        Shutdown_Evars(e);
        Free(EVARS, e);
        Init_None(iterator);
    }

    if (THROWING)  // assume Drop_Level() called on SUBLEVEL?
        return THROWN;

    Drop_Level(SUBLEVEL);

    Clear_Level_Flag(level_, NOTIFY_ON_ABRUPT_FAILURE);  // necessary?

    return DELEGATE(OUT, frame);
}}


// From %c-eval.c -- decide if this should be shared or otherwise.
//
#define Make_Action_Sublevel(parent) \
    Make_Level((parent)->feed, \
        LEVEL_FLAG_RAISED_RESULT_OK \
        | ((parent)->flags.bits & EVAL_EXECUTOR_FLAG_DIDNT_LEFT_QUOTE_TUPLE))



//
//  run: native [
//
//  {Invoke code inline as if it had been invoked via a WORD!}
//
//      return: [<opt> <void> any-value!]
//      frame [<unrun> frame!]
//      args [<opt> <void> any-value! <variadic>]
//  ]
//
DECLARE_NATIVE(run)
{
    INCLUDE_PARAMS_OF_RUN;

    Value(*) action = ARG(frame);
    UNUSED(ARG(args));  // uses internal mechanisms to act variadic

    Level* sub = Make_Action_Sublevel(level_);
    Push_Level(OUT, sub);
    Push_Action(
        sub,
        VAL_ACTION(action),
        VAL_FRAME_BINDING(action)
    );
    Begin_Prefix_Action(sub, VAL_FRAME_LABEL(action));

    return DELEGATE_SUBLEVEL(sub);
}
